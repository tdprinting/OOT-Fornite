"""Original N64-style reef meshes. Run with Python (no dependencies) to regenerate shared/lobby_fish_model.h.

Flat shaded, six-sided body rings, chunky stripe borders, fin fans and readable eyes.
Game units, +Z nose, +Y up. Blender authoring/animated GLB uses this same geometry.
"""
from pathlib import Path
import math

ROOT = Path(__file__).resolve().parents[2]
ORANGE, WHITE, BLACK = (235, 112, 28), (246, 231, 188), (27, 32, 42)
BLUE, PALE, SAND = (59, 127, 196), (165, 216, 220), (186, 168, 112)

def mesh(species):
    triangles = []
    def tri(a, b, c, color, part=0):
        triangles.append((a, b, c, color, part))
    if species == 0:
        rings = [(31, 2, 3), (24, 9, 13), (19, 12, 16), (16, 13, 17),
                 (8, 14, 18), (3, 13, 17), (0, 12, 16), (-9, 9, 13), (-15, 6, 10), (-19, 4, 6), (-27, 2, 4)]
        colors = [ORANGE, BLACK, WHITE, ORANGE, BLACK, WHITE, ORANGE, BLACK, WHITE, ORANGE]
    else:
        rings = [(35, 1.5, 2), (27, 4, 6), (17, 6, 9), (3, 7, 10), (-12, 5, 8), (-27, 2, 4)]
        colors = [BLUE]*5
    for r in range(len(rings)-1):
        z, w, h = rings[r]; zz, ww, hh = rings[r+1]
        for side in range(6):
            a = side*math.tau/6; b = (side+1)*math.tau/6
            v = [(math.sin(a)*w, math.cos(a)*h, z), (math.sin(b)*w, math.cos(b)*h, z),
                 (math.sin(b)*ww, math.cos(b)*hh, zz), (math.sin(a)*ww, math.cos(a)*hh, zz)]
            color = colors[r]
            if species == 1:
                color = BLACK if side in (1, 4) else PALE if side in (2, 3) else BLUE
            shade = (1.0, 0.91, 0.72, 0.72, 0.91, 1.0)[side]
            color = tuple(round(c*shade) for c in color)
            tri(v[0],v[1],v[2],color); tri(v[0],v[2],v[3],color)
    # Close the nose and tail so there are no holes when the fish turn towards the viewer.
    for z,w,h in (rings[0], rings[-1]):
        for side in range(6):
            a,b = side*math.tau/6,(side+1)*math.tau/6
            tri((0,0,z),(math.sin(a)*w,math.cos(a)*h,z),(math.sin(b)*w,math.cos(b)*h,z), ORANGE if species == 0 else BLUE)
    fin = ORANGE if species == 0 else BLUE
    # Broad rounded clown tail vs slender forked wrasse tail, with dark outer edging.
    fan = [(0,4,-27),(0,13 if species == 0 else 10,-40),(0,6,-44),(0,0,-40),(0,-6,-44),(0,-13 if species == 0 else -10,-40),(0,-4,-27)]
    for j in range(len(fan)-1):
        tri((0,0,-28),fan[j],fan[j+1],fin,1)
        outer_a,outer_b = fan[j],fan[j+1]
        inner_a = tuple(0.88*x if k != 2 else -28+(x+28)*0.88 for k,x in enumerate(outer_a))
        inner_b = tuple(0.88*x if k != 2 else -28+(x+28)*0.88 for k,x in enumerate(outer_b))
        tri(outer_a,outer_b,inner_a,BLACK,1);tri(inner_a,outer_b,inner_b,BLACK,1)
    for sign,part in ((1,2),(-1,3)):
        w = 12 if species == 0 else 6
        tri((sign*w,-2,12),(sign*(w+10),-9,-3),(sign*w,-5,-8),fin,part)
        tri((sign*(w+10),-9,-3),(sign*(w+8),-12,-5),(sign*w,-5,-8),BLACK,part)
    for y,part in ((1,4),(-1,0)):
        h = 17 if species == 0 else 9
        tri((0,y*h,14),(0,y*(h+9),1),(0,y*8,-22),fin,part)
        tri((0,y*(h+9),1),(0,y*(h+7),-9),(0,y*8,-22),BLACK,part)
    # Eyes sit on the cheeks, away from the head's white stripe.
    ex = 10.6 if species == 0 else 4.8
    ez = 23 if species == 0 else 25
    for s in (-1,1):
        for radius,color in ((3.8,WHITE),(2.6,BLACK),(0.8,(255,250,226))):
            x=s*(ex+(3.8-radius)*0.12)
            oy=4+(1 if radius < 1 else 0); oz=ez+(0.8 if radius < 1 else 0)
            for j in range(6):
                a,b=j*math.tau/6,(j+1)*math.tau/6
                tri((x,oy,oz),(x,oy+math.cos(a)*radius,oz+math.sin(a)*radius),
                    (x,oy+math.cos(b)*radius,oz+math.sin(b)*radius),color)
    return triangles

def tank():
    out=[]
    def tri(a,b,c,col):out.append((a,b,c,col,0))
    def box(x0,y0,z0,x1,y1,z1,col):
        p=[(x0,y0,z0),(x1,y0,z0),(x1,y1,z0),(x0,y1,z0),(x0,y0,z1),(x1,y0,z1),(x1,y1,z1),(x0,y1,z1)]
        for a,b,c,d in [(0,1,2,3),(4,7,6,5),(0,4,5,1),(3,2,6,7),(0,3,7,4),(1,5,6,2)]:
            tri(p[a],p[b],p[c],col);tri(p[a],p[c],p[d],col)
    stone=(90,117,132); trim=(169,185,161)
    box(-171,0,-98,171,25,98,stone);box(-155,25,-82,155,32,82,SAND)
    for y in (25,185):
        box(-174,y,-100,174,y+6,-94,trim);box(-174,y,94,174,y+6,100,trim)
        box(-174,y,-94,-168,y+6,94,trim);box(168,y,-94,174,y+6,94,trim)
    for x in (-170,170):
        for z in (-94,94):box(x-3,31,z-3,x+3,185,z+3,stone)
    # Triforce-like geometric ornament on the front stone plinth, original flat triangles.
    for x in (-115,0,115):
        tri((x-10,4,-98.5),(x+10,4,-98.5),(x,19,-98.5),(207,182,83))
    # Chunky coral/anemone fans; readable silhouettes without textures or transparency.
    for i in range(7):
        x=-92+(i-3)*7;z=28+(i%2)*8;h=45+(i%3)*9
        tri((x-5,32,z),(x+5,32,z),(x+6,32+h,z),(199,119,142))
        tri((x-5,32,z),(x+6,32+h,z),(x-3,32+h-5,z),(224,155,148))
    for i in range(5):
        x=82+(i-2)*12;z=20;h=38+(i%3)*12
        tri((x-4,32,z),(x+4,32,z),(x+9,32+h,z),(74,145,109))
        tri((x-4,32,z),(x+9,32+h,z),(x+2,32+h,z),(128,178,119))
    return out

def crab():
    out=[]
    def tri(a,b,c,col,part=8):out.append((a,b,c,col,part))
    red=(179,79,53);light=(218,138,90);shell=(161,150,176)
    # Rounded, low-poly borrowed shell; nose points +Z, shell trails behind.
    rings=[(-20,0.8,1),(-15,7,8),(-6,12,12),(3,8,8),(7,3,4)]
    for r in range(len(rings)-1):
        z,w,h=rings[r];zz,ww,hh=rings[r+1]
        for s in range(6):
            a,b=s*math.tau/6,(s+1)*math.tau/6
            points=[(math.sin(a)*w,14+math.cos(a)*h,z),(math.sin(b)*w,14+math.cos(b)*h,z),
                    (math.sin(b)*ww,14+math.cos(b)*hh,zz),(math.sin(a)*ww,14+math.cos(a)*hh,zz)]
            col=shell if s%2 else (192,179,189)
            tri(points[0],points[1],points[2],col);tri(points[0],points[2],points[3],col)
    # Cream spiral accents on both cheeks, raised a fraction above the shell.
    for side in (-1,1):
        for j in range(9):
            a=j*0.8;b=(j+1)*0.8;ra=7-j*0.6;rb=7-(j+1)*0.6
            p=(side*11.8,14+math.cos(a)*ra,-6+math.sin(a)*ra)
            q=(side*11.8,14+math.cos(b)*rb,-6+math.sin(b)*rb)
            tri(p,q,(side*12,14+math.cos(a)*(ra-1.1),-6+math.sin(a)*(ra-1.1)),(234,214,173))
    # Small orange body, alternating pointed walking legs and asymmetric pincers.
    tri((-8,5,2),(8,5,2),(7,8,13),red);tri((-8,5,2),(7,8,13),(-7,8,13),light)
    for side,part in ((1,5),(-1,6)):
        for z in (-4,2,8):
            tri((side*6,6,z),(side*15,5,z+2),(side*21,0,z+5),red,part)
            tri((side*6,6,z),(side*21,0,z+5),(side*14,2,z-1),light,part)
        tri((side*5,7,11),(side*14,6,17),(side*10,11,20),light,7)
        tri((side*5,7,11),(side*10,11,20),(side*5,5,19),red,7)
        tri((side*10,11,20),(side*12,9,24),(side*7,8,21),(218,167,109),7)
        # Eyestalks: broad roots and dark bright-eyed tips, not copied from game assets.
        tri((side*3,8,11),(side*5,17,14),(side*6,16,13),light)
        tri((side*4,17,13),(side*7,18,13),(side*6,20,14),(25,27,35))
        tri((side*4,17,13),(side*6,20,14),(side*4,19,14),(36,42,48))
    return out

def write_header():
    lines=['// Generated by tools/fish/make_fish.py; edit the generator, not the arrays.', '#pragma once', '#include <cstdint>', 'namespace royale::reef {',
           'struct ModelVertex { float x, y, z; uint8_t r, g, b, part; };']
    for name,data in [('Clownfish',mesh(0)),('CleanerWrasse',mesh(1)),('Aquarium',tank()),('HermitCrab',crab())]:
        lines.append(f'inline constexpr ModelVertex k{name}[] = {{')
        for a,b,c,col,part in data:
            for x,y,z in (a,b,c):
                lines.append('    {%.4ff, %.4ff, %.4ff, %d, %d, %d, %d},' % (x,y,z,*col,part))
        lines.append('};')
    lines.append('} // namespace royale::reef\n')
    (ROOT/'shared/lobby_fish_model.h').write_text('\n'.join(lines),encoding='utf-8')

if __name__=='__main__':
    write_header()
    print('Generated fish and aquarium:', len(mesh(0)), len(mesh(1)), len(tank()), 'triangles')
