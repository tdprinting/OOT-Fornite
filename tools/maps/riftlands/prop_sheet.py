"""Blender QA plate of all Astra families, using exported map geometry/materials."""
import sys,math
from pathlib import Path
HERE=Path(__file__).resolve().parent;sys.path.insert(0,str(HERE))
import world,props,geom,kit
import blender_build as builder
import bpy,numpy as np
OUT=builder.OUT

def main():
    # Reuse the exact material builder, then ground isolated inspection models flat.
    _,_,T=world.build()
    names=['crown_cistern','crown_standard','crown_planter','quarry_sled','quarry_forge','quarry_gong',
      'kakariko_well','kakariko_cart','kakariko_notice','deku_cradle','deku_granary','deku_lantern',
      'lake_skiff','lake_netrack','lake_bollard','ranch_wagon','ranch_milkstand','ranch_trough',
      'spirit_screen','spirit_stall','spirit_urn','frost_woodcrib','frost_bell','frost_sled',
      'temple_window','waystone','fairy_basin','falls_marker','lab_gauge','grave_memorial']
    w=geom.World();real=kit.H;kit.H=lambda x,z:0
    for k,name in enumerate(names):props.family(w,name,(k%6)*380,(k//6)*450)
    kit.H=real
    p=builder.pres;sc=p.reset();p.COL.clear();p.MATS.clear()
    for name,(im,_) in T.items():
        image=p.image_from(name,p.textures.native_preview(im),str(OUT/'textures'))
        p.MATS[name]=p.material(name,image,emission=1 if name in ('window_lit','lava_glow') else 0)
    p.build_structures(w);p.lights_and_world(sc)
    text_mat=bpy.data.materials.new('Label ink');text_mat.diffuse_color=(.025,.04,.05,1)
    # Readable orthographic inspection layout; labels are presentation only.
    for k,name in enumerate(names):
        data=bpy.data.curves.new(name,'FONT');data.body=name.replace('_',' ');data.size=.36;data.align_x='CENTER';data.materials.append(text_mat)
        obj=bpy.data.objects.new(name+' label',data);p.collection('Labels').objects.link(obj)
        obj.location=((k%6)*3.8,-(k//6)*4.5-1.5,.05)
    mat=bpy.data.materials.new('Inspection ground');mat.diffuse_color=(.75,.72,.62,1)
    me=bpy.data.meshes.new('Inspection ground');me.from_pydata([(-5,-25,-.03),(25,-25,-.03),(25,5,-.03),(-5,5,-.03)],[],[(0,1,2,3)])
    obj=bpy.data.objects.new('Inspection ground',me);p.collection('Presentation').objects.link(obj);me.materials.append(mat)
    cam=p.camera('Prop inspection',(950,2450,3450),(950,75,950),28,ortho=27)
    sc.camera=cam;sc.cycles.samples=20;sc.render.resolution_x=1800;sc.render.resolution_y=1400;sc.render.resolution_percentage=100
    sc.render.filepath=str(OUT/'prop-sheet.png');bpy.ops.render.render(write_still=True)
    print('RENDERED',sc.render.filepath,flush=True)

if __name__=='__main__':main()
