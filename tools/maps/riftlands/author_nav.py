"""Choose starter chest cells in the connected dry authoring floor graph.

Uses the 60-unit ground lattice; the C++ suite independently checks the exported
results, door clearances and optional upper/climbing routes.
"""
from collections import deque
import math
import numpy as np
import terrain

def reachable(w,h,obstacles):
    n=218;axis=-6500+(np.arange(n)+.5)*60;X,Z=np.meshgrid(axis,axis)
    ground=terrain.grid_height(h,X,Z);floor=ground.copy();structure=np.zeros(X.shape,bool)
    for x,z,hw,hd,y,yaw in w.buildings:
        dx,dz=X-x,Z-z;c,s=math.cos(yaw),math.sin(yaw)
        u,v=dx*c+dz*s,-dx*s+dz*c
        inside=(abs(u)<=hw)&(abs(v)<=hd);floor=np.where(inside,np.maximum(floor,y),floor)
        structure|=(abs(u)<=hw+30)&(abs(v)<=hd+30)
    for x0,z0,x1,z1,half,y0,y1 in w.walkways:
        dx,dz=x1-x0,z1-z0;L2=dx*dx+dz*dz
        t=((X-x0)*dx+(Z-z0)*dz)/L2
        inside=(t>=0)&(t<=1)&(abs((X-x0)*dz-(Z-z0)*dx)<=half*math.sqrt(L2))
        y=y0+(y1-y0)*t;floor=np.where(inside&(y>floor-40),np.maximum(floor,y),floor);structure|=inside
    sx=(terrain.grid_height(h,X+15,Z)-terrain.grid_height(h,X-15,Z))/30
    sz=(terrain.grid_height(h,X,Z+15)-terrain.grid_height(h,X,Z-15))/30
    valid=(np.hypot(X,Z)<=6500)&(floor>=terrain.water_y(X,Z)+15)&(structure|(1/np.sqrt(1+sx*sx+sz*sz)>=.8))
    for a,b,c,d in obstacles:valid&=~((X>a)&(X<b)&(Z>c)&(Z<d))
    seen=np.zeros(X.shape,bool);start=(int((1500+6500)/60),int(6500/60));queue=deque([start]);seen[start]=True
    while queue:
        z,x=queue.popleft()
        for dz,dx in [(-1,0),(1,0),(0,-1),(0,1),(-1,-1),(-1,1),(1,-1),(1,1)]:
            zz,xx=z+dz,x+dx
            if zz<0 or xx<0 or zz>=n or xx>=n or seen[zz,xx] or not valid[zz,xx]:continue
            if dx and dz and (not valid[z,xx] or not valid[zz,x]):continue
            if abs(floor[zz,xx]-floor[z,x])>70:continue
            seen[zz,xx]=True;queue.append((zz,xx))
    def accepts(x,z):
        i,j=int((x+6500)/60),int((z+6500)/60)
        return 0<=i<n and 0<=j<n and bool(seen[j,i])
    return accepts
