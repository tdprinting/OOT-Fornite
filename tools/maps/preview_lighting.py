"""Warm interior fill for Blender presentation; excluded from game geometry exports."""
import bpy, json, os

def apply(scene,buildings):
    col=bpy.data.collections.get('Presentation')
    for i,b in enumerate(buildings):
        name='Interior warm fill %02d'%i
        o=bpy.data.objects.get(name)
        if o is None:
            light=bpy.data.lights.new(name,'POINT');o=bpy.data.objects.new(name,light);col.objects.link(o)
        x,y,z=b['center_blender'];o.location=(x,y,z+2.8)
        o.data.energy=100;o.data.color=(1,.76,.48);o.data.shadow_soft_size=.55

if __name__=='__main__':
    root=os.path.abspath(os.path.join(os.path.dirname(__file__),'../..'))
    out=os.path.join(root,'assets/maps/hyrule_convergence')
    with open(os.path.join(out,'placement.json')) as f:buildings=json.load(f)['buildings']
    scene=bpy.context.scene;apply(scene,buildings)
    for filename,name in [('overview','Overview'),('top_down','Top down'),('market_detail','Market streets'),('interior','Furnished interior')]:
        scene.camera=bpy.data.objects[name];scene.render.filepath=os.path.join(out,filename+'.png');bpy.ops.render.render(write_still=True)
    scene.camera=bpy.data.objects['Overview']
    bpy.ops.wm.save_as_mainfile(filepath=os.path.join(out,'hyrule_convergence.blend'))
