"""Convert authored Blender geometry to native OoT collision and textured display data."""
import bpy, math, os, json
from collections import defaultdict
from mathutils import Vector
import numpy as np

def native_texture(im):
    """Area-filtered RGBA5551, explicitly big-endian bytes for the N64 texture uploader.

    Do not emit uint16_t texels: their host-endian memory swaps colour/alpha bits on
    Windows and Android, although tests of the numeric values still look correct.
    """
    w,h=im.size
    pixels=np.asarray(im.pixels[:],dtype=np.float32).reshape(h,w,4)
    rgb=pixels.reshape(32,h//32,32,w//32,4).mean(axis=(1,3))[:,:,:3]
    q=np.rint(np.clip(rgb,0,1)*31).astype(np.uint16)
    words=((q[:,:,0]<<11)|(q[:,:,1]<<6)|(q[:,:,2]<<1)|1)
    return list(words.astype('>u2').tobytes())

def refresh_native_textures(root):
    names=['grass','forest','sand','basalt','snow','stone','plaster','wood','roof','blue_roof','moss','path','gold','water','linen','rug','ceramic','pages','iron']
    path=os.path.join(root,'shared','convergence_model.h')
    with open(path) as f:content=f.read()
    start=content.index('alignas(8) inline constexpr uint8_t kTextures[][2048] = {')
    textures='alignas(8) inline constexpr uint8_t kTextures[][2048] = {\n'
    for name in names:
        mat=bpy.data.materials.get(name)
        im=next(n.image for n in mat.node_tree.nodes if n.type=='TEX_IMAGE') if mat else bpy.data.images.load(os.path.join(root,'assets','maps','hyrule_convergence',name+'.png'))
        textures+='{'+','.join(map(str,native_texture(im)))+'},\n'
    with open(path,'w') as f:f.write(content[:start]+textures+'};\n} }\n')

def refresh_export(root):
    """Re-export an edited saved source without rebuilding or re-rendering the artwork."""
    out=os.path.join(root,'assets','maps','hyrule_convergence')
    with open(os.path.join(out,'placement.json')) as f:placement=json.load(f)
    cols={n:bpy.data.collections[n] for n in ['Terrain','Architecture','Foliage','Props','Water']}
    terrain=bpy.data.objects['Island terrain'];grid=np.array([v.co.z for v in terrain.data.vertices]).reshape(65,65)[::-1]
    def height(x,y):
        fx=max(0,min(64,(x+74.1253)/148.2506*64));fz=max(0,min(64,(77.0572-y)/154.1144*64))
        i=min(63,int(fx));j=min(63,int(fz));u=fx-i;v=fz-j
        a,b,c,d=grid[j,i],grid[j,i+1],grid[j+1,i],grid[j+1,i+1]
        return float(a+u*(b-a)+v*(d-b) if u>=v else a+u*(d-c)+v*(c-a))
    image=next(n.image for n in terrain.data.materials[0].node_tree.nodes if n.type=='TEX_IMAGE')
    w,h=image.size;pixels=np.array(image.pixels[:]).reshape(h,w,4)
    def ground_colour(x,y):
        i=max(0,min(w-1,round((x+74.1253)/148.2506*(w-1))));j=max(0,min(h-1,round((y+77.0572)/154.1144*(h-1))))
        return tuple(float(c) for c in pixels[j,i,:3])
    names=['grass','forest','sand','basalt','snow','stone','plaster','wood','roof','blue_roof','moss','path','gold','water','linen','rug','ceramic','pages','iron']
    mats={}
    for name in names:
        m=bpy.data.materials.get(name)
        if m is None:
            m=bpy.data.materials.new(name);m.use_nodes=True;n=m.node_tree.nodes.new('ShaderNodeTexImage');n.image=bpy.data.images.load(os.path.join(out,name+'.png'))
        mats[name]=m
    pois=[(p['name'],*p['blender_xy'],'grass',p['boss_kind']) for p in placement['pois']]
    export(root,out,cols,pois,placement['markers'],placement['buildings'],height,lambda x,y:'grass',mats,ground_colour)

def export(root,out,cols,pois,markers,buildings,height,biome,mats,ground_colour):
    # Roofs must face outwards so Link can land on them, as well as see them.
    flipped=False
    for o in cols['Architecture'].objects:
        if o.type=='MESH' and 'pitched roof' in o.name and o.data.polygons[0].normal.z<0:
            o.data.flip_normals();o.data.update();flipped=True
    if flipped:bpy.ops.export_scene.gltf(filepath=os.path.join(out,'hyrule_convergence.glb'),use_selection=True,export_apply=True)
    def game(co):return tuple(round(v) for v in (co[0]*100,co[2]*100,-co[1]*100))
    def array(f,typ,name,rows):
        f.write('inline constexpr '+typ+' '+name+'[] = {\n')
        for r in rows:f.write(' {'+','.join(str(v) for v in r)+'},\n')
        f.write('};\n')
    terrain=next(o for o in cols['Terrain'].objects if o.name=='Island terrain')
    terrain.data.calc_loop_triangles()
    cv=[];cp=[];lookup={}; obstacles=[]; prop_colliders=[]; static_fixtures=[]; audit=[]
    def bounds(o):
        points=[o.matrix_world@Vector(p) for p in o.bound_box]
        return [min(p[i] for p in points) for i in range(3)],[max(p[i] for p in points) for i in range(3)]
    def obstacle(lo,hi):
        base=height((lo[0]+hi[0])/2,(lo[1]+hi[1])/2)
        if hi[2]>base+.4 and lo[2]<base+1.8:
            obstacles.append((round(lo[0]*100),round(hi[0]*100),round(-hi[1]*100),round(-lo[1]*100)))
    def vertex(co):
        g=game(co)
        if g not in lookup:lookup[g]=len(cv);cv.append(g)
        return lookup[g]
    def collision(o,simple=False):
        if simple:
            corners=[o.matrix_world@Vector(p) for p in o.bound_box]
            lo=[min(p[i] for p in corners) for i in range(3)];hi=[max(p[i] for p in corners) for i in range(3)]
            verts=[Vector((x,y,z)) for z in [lo[2],hi[2]] for y in [lo[1],hi[1]] for x in [lo[0],hi[0]]]
            faces=[(0,2,3),(0,3,1),(4,5,7),(4,7,6),(0,1,5),(0,5,4),(2,6,7),(2,7,3),(0,4,6),(0,6,2),(1,3,7),(1,7,5)]
        else:
            o.data.calc_loop_triangles();verts=[o.matrix_world@v.co for v in o.data.vertices];faces=[tuple(t.vertices) for t in o.data.loop_triangles]
        ids=[vertex(co) for co in verts]
        for face in faces:
            a,b,c=[ids[i] for i in face];A=Vector(cv[a]);B=Vector(cv[b]);C=Vector(cv[c]);n=(B-A).cross(C-A)
            if n.length<.01:continue
            n.normalize();cp.append((a,b,c,*[round(v*32767) for v in n],round(-n.dot(A))))
        if o!=terrain:
            lo=[min(p[i] for p in verts) for i in range(3)];hi=[max(p[i] for p in verts) for i in range(3)]
            static_fixtures.append((*game((lo[0],hi[1],lo[2])),*game((hi[0],lo[1],hi[2])),json.dumps(o.name)))
            # Vertical obstacles only. Floors, thresholds and overhead beams stay navigable.
            obstacle(lo,hi)
    collision(terrain)
    # Keep every building shell and tree trunk permanently solid. Furniture and
    # loose cover use the existing nearby-collision actor: the complete world would
    # otherwise exceed the scene's 13-bit collision vertex indices.
    for key in ['Architecture','Foliage']:
        for o in cols[key].objects:
            if o.type!='MESH':continue
            solid=key=='Architecture' and not o.name.split('.')[0].endswith(' oak post') or o.name.startswith('Tree trunk')
            if solid:
                collision(o);audit.append({'name':o.name,'collision':'static'})
    groups={}
    for o in cols['Props'].objects:
        if o.type!='MESH' or o.name.startswith(('Loot chest','Chest band','Geometric woven carpet','Pillow','Firewood','Gable brace','Carved door','Door oak','Tower gold')):continue
        lo,hi=bounds(o);name=o.name.split('.')[0]
        role=next((r for r in ['bed','table','bench','shelf','hearth'] if r in name.lower()),None)
        if name=='Woven bedcover':role='bed'
        if name=='Table jug':role=None
        if role:
            b=min(buildings,key=lambda b:(b['center_blender'][0]-(lo[0]+hi[0])/2)**2+(b['center_blender'][1]-(lo[1]+hi[1])/2)**2)
            key=(b['name'],tuple(b['center_blender']),role)
        elif min(hi[0]-lo[0],hi[1]-lo[1])>=.34 and hi[2]-lo[2]>=.24:
            key=(o.name,)
        else:continue  # small loose books/pottery and soft trim are decorative
        if key not in groups:groups[key]=[lo,hi,[]]
        g=groups[key];g[0]=[min(a,b) for a,b in zip(g[0],lo)];g[1]=[max(a,b) for a,b in zip(g[1],hi)];g[2].append(o.name)
    for lo,hi,names in groups.values():
        low=game((lo[0],hi[1],lo[2]));high=game((hi[0],lo[1],hi[2]))
        prop_colliders.append((*low,*high,json.dumps(names[0])))
        obstacle(lo,hi)
        audit.extend({'name':n,'collision':'nearby'} for n in names)
    # The fountain is a shallow walkable platform, not a hole or an exit.
    fountain=next(o for o in cols['Terrain'].objects if o.name=='Market fountain basin')
    collision(fountain);flo,fhi=bounds(fountain)
    obstacles=list(dict.fromkeys(obstacles))
    assert len(cv)<8192,('Collision exceeds 13-bit vertex limit',len(cv))
    assert len(cp)<65536
    # Grid coordinates follow the existing island machinery. Blender Y is the negative game Z.
    heights=[]
    for j in range(65):
        for i in range(65):heights.append(round(height(-74.1253+i*148.2506/64,77.0572-j*154.1144/64)*100))
    palette={'grass':(81,111,48),'forest':(43,77,35),'sand':(184,143,84),'basalt':(63,63,70),'snow':(209,222,224),'moss':(77,91,62)}
    colors=[]
    for j in range(385):
        for i in range(385):
            x=-74.1253+i*148.2506/384;y=77.0572-j*154.1144/384
            colors.extend(round(v*255) for v in ground_colour(x,y))
    cover=''
    for j in range(256):
        for i in range(256):
            x=-74.1253+(i+.5)*148.2506/256;y=77.0572-(j+.5)*154.1144/256
            # Authored trees provide cover; do not procedurally grow scenery through entrances.
            cover+='0' if height(x,y)<-2.27 else '3'
    path=os.path.join(root,'shared','convergence_data.h')
    with open(path,'w') as f:
        f.write('// GENERATED by tools/maps/build_convergence.py. Edit the Blender authoring script.\n#pragma once\n#include <cstdint>\nnamespace royale { namespace convergence {\n')
        f.write('inline constexpr int16_t kHeights[] = {'+','.join(map(str,heights))+'};\n')
        f.write('inline constexpr uint8_t kColours[] = {'+','.join(map(str,colors))+'};\n')
        f.write('inline constexpr char kCover[] =\n'+''.join('"'+cover[i:i+256]+'"\n' for i in range(0,len(cover),256))+';\n')
        f.write('struct Vertex { int16_t x,y,z; };\nstruct Triangle { uint16_t a,b,c; int16_t nx,ny,nz,dist; };\n')
        array(f,'Vertex','kCollisionVertices',cv);array(f,'Triangle','kCollisionTriangles',cp)
        f.write('struct PropCollider { int16_t x0,y0,z0,x1,y1,z1; const char* name; };\n')
        array(f,'PropCollider','kPropColliders',prop_colliders)
        array(f,'PropCollider','kStaticFixtures',static_fixtures)
        f.write('struct FloorPatch { float x,z,radius,y; };\n')
        array(f,'FloorPatch','kFloorPatches',[(round((flo[0]+fhi[0])*50),round(-(flo[1]+fhi[1])*50),round((fhi[0]-flo[0])*50),round(fhi[2]*100))])
        f.write('struct Obstacle { float x0,x1,z0,z1; };\n');array(f,'Obstacle','kObstacles',obstacles)
        f.write('struct Building { float x,z,halfWidth,halfDepth,floorY; };\n')
        array(f,'Building','kBuildings',[(round(b['center_blender'][0]*100),round(-b['center_blender'][1]*100),b['size_blender'][0]*50,b['size_blender'][1]*50,round((b['center_blender'][2]+.11)*100)) for b in buildings])
        f.write('struct Point { float x,z; };\n');array(f,'Point','kLootSites',[(round(m['game_position'][0]),round(m['game_position'][2])) for m in markers if m['kind']=='loot'])
        f.write('struct Region { const char* name; float x,z; int boss; };\ninline constexpr Region kRegions[] = {\n')
        kinds={'Stone':0,'Lava':1,'Frost':2,'Moss':3,'Tide':4,'Shade':5,'Dune':6}
        for n,x,y,mat,b in pois:f.write(' {"%s",%d,%d,%d},\n'%(n,x*100,-y*100,kinds.get(b,-1)))
        f.write('};\n} }\n')
    # Native textured vertices, no runtime GLB loader required. Group by spatial chunk and material.
    names=list(mats);groups=defaultdict(list);sun=Vector((-.4,-.6,.7)).normalized()
    for key in ['Architecture','Props','Foliage','Terrain']:
        for o in cols[key].objects:
            if o.type!='MESH' or o==terrain or o.name.startswith(('Loot chest','Chest band')):continue
            me=o.data;me.calc_loop_triangles();uv=me.uv_layers.active
            for tri in me.loop_triangles:
                pos=[o.matrix_world@me.vertices[i].co for i in tri.vertices];center=sum(pos,Vector())/3
                mat=me.materials[tri.material_index];mi=names.index(mat.name)
                chunk=(max(0,min(7,int((center.x+74.1253)/18.531325))),max(0,min(7,int((-center.y+77.0572)/19.2643))))
                shade=.65+.35*max(0,(o.matrix_world.to_3x3()@tri.normal).normalized().dot(sun))
                for co,li in zip(pos,tri.loops):
                    tex=uv.data[li].uv if uv else (co[0]/3,co[1]/3)
                    groups[(*chunk,mi)].append((*game(co),round(tex[0]*1024),round(tex[1]*1024),round(255*shade),round(247*shade),round(231*shade)))
    draw=[];batches=[]
    for (cx,cz,mi),verts in sorted(groups.items()):
        start=len(draw);draw.extend(verts);batches.append((start,len(verts),mi,round(-7412.53+(cx+.5)*1853.1325),round(-7705.72+(cz+.5)*1926.43)))
    with open(os.path.join(root,'shared','convergence_model.h'),'w') as f:
        f.write('// GENERATED by tools/maps/build_convergence.py.\n#pragma once\n#include <cstdint>\nnamespace royale { namespace convergence {\nstruct DrawVertex { int16_t x,y,z,s,t; uint8_t r,g,b; };\n')
        array(f,'DrawVertex','kDrawVertices',draw)
        f.write('struct Batch { uint32_t first,count; uint16_t texture; int16_t x,z; };\n');array(f,'Batch','kBatches',batches)
        f.write('// RGBA5551 texture bytes, high byte first; independent of host byte order.\n')
        f.write('alignas(8) inline constexpr uint8_t kTextures[][2048] = {\n')
        for name in names:
            im=next(n.image for n in mats[name].node_tree.nodes if n.type=='TEX_IMAGE');vals=native_texture(im)
            f.write('{'+','.join(map(str,vals))+'},\n')
        f.write('};\n} }\n')
    report={'collision_vertices':len(cv),'collision_triangles':len(cp),'nearby_prop_colliders':len(prop_colliders),'draw_triangles':len(draw)//3,'draw_batches':len(batches),'navigation_obstacles':len(obstacles),'loading_zone_surfaces':0,'texture_count':len(names)}
    with open(os.path.join(out,'export_report.json'),'w') as f:json.dump(report,f,indent=2)
    with open(os.path.join(out,'collision_audit.json'),'w') as f:json.dump(audit,f,indent=2)
    print('GAME EXPORT',report)
