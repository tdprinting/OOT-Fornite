"""blender --background --python tools/maps/riftlands/blender_build.py.

Packed .blend, portable .glb, aerial/top and grounded previews from exactly the
world exported to the game. --quick uses fewer Cycles samples and smaller preview resolution.
"""
import sys, importlib.util, math, json
from pathlib import Path
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
import world,terrain,layout
import bpy,numpy as np
spec=importlib.util.spec_from_file_location('presentation',HERE.parent/'kingdom'/'blender_build.py')
pres=importlib.util.module_from_spec(spec);spec.loader.exec_module(pres)
OUT=HERE.parents[2]/'assets'/'maps'/'hyrule_riftlands'
SHOTS={
 'overview':((0,11400,15200),(0,400,-300),35),
 'top':((0,24000,0),(0,0,0),None),
 'castle':((3000,2850,-1000),(0,1700,-4100),24),
 'courtyard':((600,1100,-3060),(-250,970,-3490),24),
 'crown_props':((-180,995,-3110),(-450,865,-3300),28),
 'kakariko':((2550,650,500),(3900,350,-650),26),
 'deku':((2700,720,3700),(4400,700,2900),25),
 'lake':((-1600,600,5750),(0,0,4200),28),
 'bazaar':((-3100,900,600),(-4550,350,-700),28),
 'ranch':((-2700,710,2450),(-3900,360,3200),28),
 'lodge':((-2200,1350,-2900),(-3550,850,-4100),28),
 'quarry':((3600,2400,-2850),(4550,1450,-4800),28),
 'interior':((3604,264,-843),(3370,244,-995),22),
}

def main():
    if '--rerender' in sys.argv:
        # Reframe named views from the final packed source without rebuilding it.
        bpy.ops.wm.open_mainfile(filepath=str(OUT/'hyrule_riftlands.blend'))
        sc=bpy.context.scene
        names=sys.argv[sys.argv.index('--rerender')+1].split(',')
        from mathutils import Vector
        for name in names:
            eye,target,lens=SHOTS[name];cam=bpy.data.objects['Camera '+name]
            cam.location=pres.B(eye);cam.rotation_euler=(Vector(pres.B(target))-cam.location).to_track_quat('-Z','Y').to_euler();cam.data.lens=lens
            sc.camera=cam;sc.render.filepath=str(OUT/(name+'.png'));bpy.ops.render.render(write_still=True)
            print('RENDERED',sc.render.filepath,flush=True)
        sc.camera=bpy.data.objects['Camera overview']
        bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'hyrule_riftlands.blend'),compress=True)
        return
    quick='--quick' in sys.argv
    OUT.mkdir(parents=True,exist_ok=True)
    w,h,T=world.build();sc=pres.reset();pres.COL.clear();pres.MATS.clear()
    for c in ['Terrain','Water','Architecture','Props','Foliage','Presentation']:pres.collection(c)
    texdir=OUT/'textures';texdir.mkdir(exist_ok=True)
    for n,(im,units) in T.items():
        image=pres.image_from(n,pres.textures.native_preview(im),str(texdir))
        pres.MATS[n]=pres.material(n,image,emission=2 if n=='lava_glow' else 1 if n=='window_lit' else 0)
    pres.build_terrain(h,T,n=385,clip_seabed=False)
    pres.build_structures(w);pres.build_collision(w)
    # Baked vertex lighting is a multiplier, not another sRGB albedo conversion.
    for obj in sc.objects:
        if obj.type=='MESH' and obj.name!='Island terrain':
            attr=obj.data.color_attributes.get('Light')
            if attr:
                values=np.empty(len(attr.data)*4,dtype=np.float32);attr.data.foreach_get('color',values)
                values=values.reshape(-1,4);values[:,:3]=values[:,:3]**(1/2.2);attr.data.foreach_set('color',values.ravel())
    # Exact raised water boxes; sea uses the existing full map sheet.
    for k,(x0,x1,z0,z1,y) in enumerate(layout.POOLS):
        me=bpy.data.meshes.new('Pool surface')
        me.from_pydata([pres.B((x0,y,z0)),pres.B((x1,y,z0)),pres.B((x1,y,z1)),pres.B((x0,y,z1))],[],[(0,3,2,1)])
        o=bpy.data.objects.new('River pool '+str(k),me);pres.collection('Water').objects.link(o)
        me.materials.append(bpy.data.materials['water_sheet'])
    pres.lights_and_world(sc)
    # A blue daylight sky and larger ocean keep the presentation off the map edges.
    nt=sc.world.node_tree;nt.nodes.clear()
    bg=nt.nodes.new('ShaderNodeBackground');bg.inputs['Color'].default_value=(.32,.52,.8,1);bg.inputs['Strength'].default_value=.65
    out=nt.nodes.new('ShaderNodeOutputWorld');nt.links.new(bg.outputs[0],out.inputs['Surface'])
    sea=bpy.data.objects['Water'];sea.scale=(8,8,1)
    ocean=sea.data.materials[0];ocean.node_tree.nodes.get('Principled BSDF').inputs['Base Color'].default_value=(.015,.13,.23,1)
    ocean.node_tree.nodes.get('Principled BSDF').inputs['Alpha'].default_value=1
    nt=ocean.node_tree;bs=nt.nodes.get('Principled BSDF')
    geom_node=nt.nodes.new('ShaderNodeNewGeometry');distance=nt.nodes.new('ShaderNodeVectorMath');distance.operation='DISTANCE';distance.inputs[1].default_value=(0,-47,-2.27)
    div=nt.nodes.new('ShaderNodeMath');div.operation='DIVIDE';div.inputs[1].default_value=85
    ramp=nt.nodes.new('ShaderNodeValToRGB');ramp.color_ramp.elements[0].position=.12;ramp.color_ramp.elements[0].color=(.035,.43,.43,1);ramp.color_ramp.elements[1].position=.65;ramp.color_ramp.elements[1].color=(.015,.13,.23,1)
    nt.links.new(geom_node.outputs['Position'],distance.inputs[0]);nt.links.new(distance.outputs['Value'],div.inputs[0]);nt.links.new(div.outputs[0],ramp.inputs[0]);nt.links.new(ramp.outputs[0],bs.inputs['Base Color'])

    sc.render.engine='CYCLES' if not quick else 'CYCLES'
    sc.cycles.samples=20 if not quick else 8
    sc.render.resolution_x=1536 if not quick else 1024
    sc.render.resolution_y=1024 if not quick else 683
    sc.render.resolution_percentage=100
    sc.view_settings.exposure=0
    shots=list(SHOTS) if not quick or '--all-views' in sys.argv else ['overview','castle','deku','lake']
    for n in shots:
        eye,target,lens=SHOTS[n]
        cam=pres.camera('Camera '+n,eye,target,lens or 28,ortho=153 if lens is None else None)
        if lens is None:cam.location=(0,0,180);cam.rotation_euler=(0,0,0)
        sc.camera=cam;sc.render.filepath=str(OUT/(n+'.png'))
        bpy.ops.render.render(write_still=True)
        print('RENDERED',sc.render.filepath,flush=True)
    # Collision is editable in Blender but excluded from portable scenery.
    bpy.ops.object.select_all(action='DESELECT')
    for o in sc.objects:
        if o.type=='MESH' and not o.hide_render:o.select_set(True)
    sc.camera=bpy.data.objects['Camera overview']
    bpy.ops.file.pack_all()
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'hyrule_riftlands.blend'),compress=True)
    bpy.ops.export_scene.gltf(filepath=str(OUT/'hyrule_riftlands.glb'),export_format='GLB',use_selection=True)
    (OUT/'model-report.json').write_text(json.dumps({'triangles':len(w.tris),'collision_vertices':len(w.col_verts),
        'collision_triangles':len(w.col_tris),'buildings':len(w.buildings),'walkways':len(w.walkways),
        'loot_candidates':len(w.loot),'renders':shots,'packed_images':len(bpy.data.images)},indent=2))

if __name__=='__main__':main()
