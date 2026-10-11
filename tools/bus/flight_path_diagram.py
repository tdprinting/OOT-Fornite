"""Flight-path diagram over Moonfall Isle: the corridor rule and four seeded sample lines, from tools/maps/moonfall/layout.py numbers."""
from PIL import Image, ImageDraw, ImageFont
import math, random
HALF_X, HALF_Z = 7412.53, 7705.72
POIS = [("Hyrule Castle",200,-4600,1000),("Clock Town",2900,-400,900),("Temple of Time",-200,100,750),("Snowhead Lodge",-3700,-4300,800),
("Spirit Colossus",-4900,-2200,950),("Goron Forge",3900,-3900,850),("Kakariko Windmill",4600,-2100,850),("Ikana Graveyard",4800,-3050,380),
("Romani Ranch",-4600,1300,850),("Great Bay Stilts",-300,3800,700),("Deku Tree Village",4300,2900,900),("Lost Woods",2700,1700,550),
("Astral Observatory",-2600,-900,350),("Zora Cape",-4000,3900,600),("Castle Falls",1400,-3450,420),("Moonfall Span",-1700,-4400,350),
("Milk Road",-2550,1900,350),("Fairy Fountain",-2200,-3200,300),("Termina Bridge",1450,2300,400),("Lantern Bridge",1300,-500,300),
("Moon Crater",-1600,2200,400),("Frozen Pond",-2300,-5000,330),("Spirit Oasis",-5600,-600,400),("Fisherman's Hut",1000,4800,300)]
Wpx = 1100; S = Wpx / (2*(HALF_X+2200)); Hpx = int(2*(HALF_Z+2200)*S)
img = Image.new("RGB", (Wpx, Hpx), (36, 78, 120)); d = ImageDraw.Draw(img)
def P(x, z): return (Wpx/2 + x*S, Hpx/2 + z*S)
R = 6400.0; MARGIN = 1500; CORR = 0.45
d.ellipse([P(-R,-R), P(R,R)], fill=(98,150,80), outline=(70,110,60), width=3)            # the island, roughly
d.ellipse([P(-R*CORR,-R*CORR), P(R*CORR,R*CORR)], outline=(240,230,180), width=2)         # the corridor: every line passes through this
d.ellipse([P(-R*0.92,-R*0.92), P(R*0.92,R*0.92)], outline=(250,250,250), width=1)         # jump opens here
for name,x,z,r in POIS:
    d.ellipse([P(x-r*0.35,z-r*0.35), P(x+r*0.35,z+r*0.35)], fill=(150,120,70), outline=(80,60,30))
try: f = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", 12); fb = ImageFont.truetype("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", 15)
except Exception: f = fb = ImageFont.load_default()
for name,x,z,r in POIS: d.text((P(x,z)[0]+6, P(x,z)[1]-7), name, fill=(255,255,240), font=f)
cols = [(255,230,120),(255,120,110),(140,220,255),(220,160,255)]
rnd = random.Random(7)
for k in range(4):
    bearing = rnd.uniform(0, 2*math.pi); off = rnd.uniform(-CORR*R, CORR*R)
    nx, nz = math.cos(bearing), math.sin(bearing); tx, tz = -nz, nx
    half = math.sqrt(R*R - off*off) + MARGIN
    a = (nx*off - tx*half, nz*off - tz*half); b = (nx*off + tx*half, nz*off + tz*half)
    length = 2*half; speed = max(120, min(300, length/50)); secs = length/speed
    d.line([P(*a), P(*b)], fill=cols[k], width=4)
    d.ellipse([P(a[0]-250,a[1]-250), P(a[0]+250,a[1]+250)], fill=cols[k])
    d.text((P(*a)[0]+8, P(*a)[1]+8), f"seed {k+1}: {length:.0f} units, {speed:.0f} u/s, {secs:.0f} s", fill=cols[k], font=fb)
d.text((12, 10), "Moonfall Bus flight lines: start 1500 outside the island, pass inside the cream ring, jumping opens inside the white ring", fill=(255,255,255), font=fb)
d.text((12, 30), "North is up. Bus altitude 3200 above the median floor; glide reach from it is about 1750 units either side of the line.", fill=(230,230,230), font=f)
import sys; img.save(sys.argv[1] if len(sys.argv) > 1 else "flight-paths.png"); print("ok")
