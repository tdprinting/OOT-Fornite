"""Compile actual patched renderer functions with a deterministic backend.

Reproduces pending triangles drawn with a newly compiled shader's wrong stride,
and material uploads leaking their active unit into framebuffer operations.
The harness extracts real function bodies; it does not duplicate the fixes.
Run after applying engine patches. Use --cxx cl from a VS developer shell.
"""
from pathlib import Path
import argparse
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
lus = Path(os.environ.get('ROYALE_ENGINE_SOURCE',str(root/'third_party/Shipwright-Android')))/'libultraship'

def extract(text, start):
    begin = text.index(start)
    brace = text.index('{', begin)
    depth = 1
    end = brace+1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[begin:end]

pc = (lus/'src/graphic/Fast3D/gfx_pc.cpp').read_text()
gl = (lus/'src/graphic/Fast3D/gfx_opengl.cpp').read_text()
lookup = extract(pc, 'static struct ShaderProgram* gfx_lookup_or_create_shader_program(')
prototype = next(line for line in pc.splitlines() if line.startswith('static void gfx_bind_surface_textures(') and line.endswith(';'))
handler = prototype+'\n'+extract(pc, 'static void gfx_bind_surface_textures(const GfxSurfaceMap* map,')+'\n'+extract(pc, 'bool gfx_surface_map_handler(')
reset = extract(gl, 'static void gfx_opengl_reset_texture_unit(')
key = extract(pc, 'struct SurfaceTextureKey {')+';'
header = (lus/'include/libultraship/surface_map.h').read_text()

prefix = r'''
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <tuple>
#include <vector>
#include <stdexcept>
struct ShaderProgram { int id; size_t stride; };
ShaderProgram old_program{1,12}, new_program{2,32};
ShaderProgram* bound_program = &old_program;
struct { ShaderProgram* shader_program = &old_program; } rendering_state;
size_t pending = 0, pending_stride = 12;
bool corrupted = false, cached = false;
std::vector<int> draw_programs;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void gfx_flush() {
    if (pending) {
        // Changing 12 floats/vertex to 32 before drawing reads each subsequent
        // vertex at the wrong offset (or beyond the uploaded buffer).
        corrupted |= bound_program->stride != pending_stride;
        draw_programs.push_back(bound_program->id);
        pending = 0;
    }
}
constexpr int GL_TEXTURE0 = 0;
int active_unit = 0;
std::array<uint32_t,8> bound_textures{11,12,13,14,15,16,0,0};
int uploads = 0;
void glActiveTexture(int unit) { active_unit = unit; }
struct API {
    ShaderProgram* (*lookup_shader)(uint64_t,uint32_t);
    void (*unload_shader)(ShaderProgram*);
    ShaderProgram* (*create_and_load_new_shader)(uint64_t,uint32_t);
    const char* (*get_name)();
    uint32_t (*new_texture)();
    void (*select_texture)(int,uint32_t);
    void (*upload_texture)(const uint8_t*,uint32_t,uint32_t);
    void (*set_sampler_parameters)(int,bool,uint32_t,uint32_t);
    void (*reset_texture_unit)();
};
constexpr uint32_t G_TX_WRAP = 0;
ShaderProgram* mock_lookup(uint64_t,uint32_t) { return cached ? &new_program : nullptr; }
void mock_unload(ShaderProgram*) {}
ShaderProgram* mock_create(uint64_t,uint32_t) { bound_program=&new_program; return &new_program; }
const char* mock_name() { return "OpenGL"; }
uint32_t mock_new_texture() { static uint32_t next=100; return next++; }
void mock_select(int unit,uint32_t id) { active_unit=unit; bound_textures[unit]=id; }
void mock_upload(const uint8_t*,uint32_t,uint32_t) { ++uploads; }
void mock_sampler(int unit,bool,uint32_t,uint32_t) { active_unit=unit; }
struct F3DGfx { struct { uintptr_t w1; } words; };
'''
middle = r'''
API api{mock_lookup,mock_unload,mock_create,mock_name,mock_new_texture,mock_select,mock_upload,mock_sampler,gfx_opengl_reset_texture_unit};
API* gfx_rapi=&api;
static const GfxSurfaceMap* surface_map=nullptr;
static std::map<SurfaceTextureKey,uint32_t> surface_textures;
'''
suffix = r'''
int main(int argc,char** argv) {
    try {
        const bool texture_test=argc>1 && std::strcmp(argv[1],"texture")==0;
        if (!texture_test) {
            pending=3; pending_stride=old_program.stride;
            auto* result=gfx_lookup_or_create_shader_program(1,0x80000000u);
            gfx_flush(); // finish frame / next material
            require(result==&new_program,"new shader missing");
            require(!corrupted,"queued vertices consumed with new shader stride");
            require(draw_programs==std::vector<int>{1},"old triangles must draw with old program");
            cached=true; pending=1; pending_stride=new_program.stride;
            const size_t before=draw_programs.size();
            gfx_lookup_or_create_shader_program(1,0x80000000u);
            require(draw_programs.size()==before && pending==1,"cache hit must not flush needlessly");
            gfx_flush(); require(!corrupted,"cached shader corrupted vertices");
        } else {
            static const uint8_t normal[4]={128,128,255,255}, height[4]={128,128,128,255};
            GfxSurfaceMap material{}; material.normal=normal; material.height=height;
            material.width=material.heightPixels=1;
            F3DGfx command{{reinterpret_cast<uintptr_t>(&material)}}; F3DGfx* ptr=&command;
            gfx_surface_map_handler(&ptr);
            require(active_unit==0,"private material texture unit leaked");
            require(bound_textures[0]==11,"colour texture binding changed");
            require(bound_textures[6]>=100 && bound_textures[7]>=100,"material textures missing");
            const auto normal_id=bound_textures[6], height_id=bound_textures[7];
            // The real framebuffer helper binds to whichever unit is active.
            bound_textures[active_unit]=999;
            require(bound_textures[6]==normal_id && bound_textures[7]==height_id,"framebuffer overwrote material binding");
            gfx_surface_map_handler(&ptr);
            require(uploads==2,"cached maps uploaded again");
            require(active_unit==0,"cached material update leaked texture unit");
            command.words.w1=0; gfx_surface_map_handler(&ptr);
            require(surface_map==nullptr,"material scope failed to reset");
        }
        std::cout << "renderer state regression passed\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << e.what() << "\n"; return 1; }
}
'''

parser=argparse.ArgumentParser()
parser.add_argument('--cxx', default=os.environ.get('CXX','c++'))
args=parser.parse_args()
with tempfile.TemporaryDirectory(prefix='surface-state-test-',dir=root.parent) as scratch:
    dest=Path(scratch)
    assert dest.resolve().parent==root.parent.resolve()
    def build(name, shader_lookup, material_handler):
        source=dest/(name+'.cpp')
        source.write_text(header+'\n'+prefix+'\n'+reset+'\n'+key+'\n'+middle+'\n'+shader_lookup+'\n'+material_handler+'\n'+suffix)
        exe=dest/(name+('.exe' if os.name=='nt' else ''))
        if Path(args.cxx).name.lower() in ('cl','cl.exe'):
            command=[args.cxx,'/nologo','/std:c++20','/EHsc',str(source),'/Fe:'+str(exe),'/Fo:'+str(dest/(name+'.obj'))]
        else:
            command=[args.cxx,'-std=c++20','-Wall','-Wextra',str(source),'-o',str(exe)]
        result=subprocess.run(command,capture_output=True,text=True)
        if result.returncode: raise RuntimeError(result.stdout+result.stderr)
        return exe
    fixed=build('fixed',lookup,handler)
    for test in ('shader','texture'):
        subprocess.run([str(fixed),test],check=True)
    # Negative controls prove that both hazards reproduce before these fixes.
    unfixed_lookup=lookup.replace('        gfx_flush();\n','')
    unfixed_handler=handler.replace('    if (gfx_rapi->reset_texture_unit) gfx_rapi->reset_texture_unit();\n','')
    before=build('before_fix',unfixed_lookup,unfixed_handler)
    for test in ('shader','texture'):
        result=subprocess.run([str(before),test],capture_output=True,text=True)
        if result.returncode==0: raise RuntimeError('Regression failed to reproduce original '+test+' hazard')
        print('Original '+test+' bug reproduced: '+result.stderr.strip())
