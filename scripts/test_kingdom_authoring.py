"""Checks authored stairs and building separation without Blender or generated data."""
from pathlib import Path
import math,sys
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/maps/kingdom'))
import kit,geom
kit.HGRID=np.zeros((65,65),dtype=np.int32)
# The former short-room branch left the ramp sticking out through a wall.
for W,D in ((440,440),(380,360)):
    w=geom.World()
    kit.house(w,0,0,W,D,style=kit.HouseStyle(roof_kind='flat'),furnish=False,loot=False,name='test')
    for x,y,z in w.col_verts:
        if 30<y<284: assert abs(x)<=W/2 and abs(z)<=D/2,(x,y,z)
    for a,b,c,surface in w.col_tris:
        A,B,C=(np.array(w.col_verts[i],dtype=float) for i in (a,b,c))
        n=np.cross(B-A,C-A)
        if n[1]>1 and max(A[1],B[1],C[1])<=284:
            assert n[1]/np.linalg.norm(n)>=1/math.sqrt(1+0.69**2)
    assert len(w.col_tris)>30
# Rotation-aware separation works across the yaw seam and keeps adjacent roofs apart.
w=geom.World()
kit.house(w,0,0,420,380,0.2,furnish=False,loot=False,name='test')
x,z=kit.clear_site(w,250,250,420,380,-0.4,'Ordon house test')
assert not kit.roofs_overlap((x,z,260,240,-0.4),(0,0,260,240,0.2))
assert math.hypot(x-250,z-250)<=1000
print('Kingdom authoring: short stairs stay indoors; roof separation passed')
