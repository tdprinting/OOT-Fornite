"""Compile and link the exact material GLSL extension for desktop and Android.

Extracts the extension from the renderer patch, so this cannot accidentally test
a separate copy of the material shader. Requires glslangValidator in PATH.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
patch = (root/'patches/libultraship/0003-item-surface-maps.patch').read_text()
sections = patch.split('diff --git ')[1:]

def additions(suffix):
    section = next(s for s in sections if s.splitlines()[0].endswith(suffix))
    return '\n'.join(line[1:] for line in section.splitlines() if line.startswith('+') and not line.startswith('+++'))

vs = additions('default.shader.vs')
fs = additions('default.shader.fs')
body = fs.split('    @if(o_surface_map)\n', 1)[1].split('    @end', 1)[0]
attributes = ('Position', 'Normal', 'Light', 'Color', 'Ambient')
validator = shutil.which('glslangValidator')
if not validator:
    raise SystemExit('glslangValidator is required (Ubuntu: glslang-tools).')

with tempfile.TemporaryDirectory(prefix='surface-shaders-') as scratch:
    dest = Path(scratch)
    assert dest.resolve().parent == Path(tempfile.gettempdir()).resolve()
    for version in ('130', '300 es', '410 core'):
        for alpha in (False, True):
            prefix = '#version '+version+'\n'
            if 'es' in version: prefix += 'precision highp float;\n'
            vert = prefix+'in vec4 aVtxPos;\n'
            vert += '\n'.join(f'in vec4 aSurface{a};\nout vec4 vSurface{a};' for a in attributes)
            vert += '\nvoid main() {\ngl_Position = aVtxPos;\n'
            vert += '\n'.join(f'vSurface{a} = aSurface{a};' for a in attributes)+'\n}\n'
            frag = prefix+'\n'.join(f'in vec4 vSurface{a};' for a in attributes)
            frag += '\nuniform sampler2D uSurfaceNormal, uSurfaceHeight;\nout vec4 outputColor;\nvoid main() {\n'
            frag += ('vec4 texel = vec4(0.6);\n' if alpha else 'vec3 texel = vec3(0.6);\n')
            frag += body.replace('@{texture}', 'texture')
            frag += ('outputColor = texel;' if alpha else 'outputColor = vec4(texel, 1.0);')+'\n}\n'
            v, f = dest/'material.vert', dest/'material.frag'
            v.write_text(vert); f.write_text(frag)
            result = subprocess.run([validator, '-l', str(v), str(f)], capture_output=True, text=True)
            if result.returncode:
                print(result.stdout, result.stderr)
                raise SystemExit(result.returncode)
            print(f'GLSL {version}, alpha={alpha}: compiled and linked')
