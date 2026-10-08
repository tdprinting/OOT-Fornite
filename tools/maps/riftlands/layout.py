"""Riftlands authoring coordinates, game x/east, y/up, z/south.

Keep the engine's 64-cell envelope; the playable island is compacted inside it.
Run speed is 100 units/sec, so adjacent POI routes take about 45–65 seconds.
"""
HALF_X, HALF_Z, CELLS, SUB = 7412.53, 7705.72, 64, 6
WATER_Y, SEA_FLOOR = -227, -560
BOSS_KINDS = {"Stone": 0, "Lava": 1, "Frost": 2, "Moss": 3, "Tide": 4, "Shade": 5, "Dune": 6}
POIS = [
 ("Crownfall Castle",0,-3850,1000,"Stone","teal spires above the waterfall terrace"),
 ("Death Mountain Quarry",4350,-3550,900,"Lava","basalt quarry and forge"),
 ("Kakariko Windmill",3900,-650,1000,None,"orange roofs and riverside lanes"),
 ("Deku Hollow",4400,2900,1100,"Moss","root halls and timber homes"),
 ("Lake Lantern",0,4200,1100,"Tide","boardwalk fishing village"),
 ("Lon Lon Crossroads",-3900,3000,1100,None,"golden paddocks and barns"),
 ("Spirit Bazaar",-4550,-700,1100,"Dune","sandstone gate and colonnades"),
 ("Frostwatch Lodge",-3550,-4100,950,"Frost","warm lodge beneath snowy pines"),
 ("Temple of Time",-450,0,750,None,"central broken sanctuary"),
 ("Fairy Spring",3000,2350,300,None,"forest recruitment shelter"),
 ("Zora's Falls",1600,-2650,450,None,"cascade beneath the royal shelf"),
 ("Goron Lookout",3200,-2550,400,None,"quarry bypass lookout"),
 ("Lakeside Lab",-1800,4500,400,None,"shore lab and tower"),
 ("Graveyard Annex",5100,-1850,420,"Shade","optional Dead Hand arena"),
 ("Castle Gardens",-850,-3000,450,None,"hedges and terrace walls"),
 ("River Overlook",3100,-650,300,None,"village river terrace"),
 ("West Stone Crossing",-1950,2450,400,None,"dry stone crossing"),
 ("East Stone Crossing",1850,3100,400,None,"dry forest crossing"),
 ("Orchard Rest",-2700,3100,400,None,"ranch recruitment shelter"),
 ("Oasis Caravan",-3650,500,350,None,"bazaar recruitment shelter"),
 ("Lantern Shore",-1500,3500,350,None,"lake recruitment shelter"),
 ("North Meadow",0,-1800,600,None,"ruins and distributed cover"),
 ("South Meadow",-300,1850,650,None,"dry final circles"),
 ("Frozen Trail",-2200,-4200,400,None,"graded snow transition"),
]
assert len(POIS) == 24
# Building shelves retain the shared kit's ordinary ramps and door clearances.
PADS = [(0,-3900,1350,820,450),(-3550,-4100,1050,640,650),
 (4300,-3050,700,550,500),(3900,-650,1180,150,550),
 (5100,-1850,430,230,380),(4400,2900,1050,140,600),
 (-3900,3000,1200,230,620),(-4550,-700,1050,140,620),
 (-450,0,680,220,600),(-1800,4500,420,30,450)]
MOUNTAINS = [(4550,-4800,2250,1950,.24),(-4300,-5150,1750,1550,0),
 (-2800,-5500,2100,1500,0),(-1500,-5650,1250,1200,0)]
RING = [(-2900,-3200),(0,-2500),(3100,-2200),(4000,-650),(3500,1700),
 (2400,3200),(-2100,3200),(-3600,2100),(-3350,-700),(-2900,-3200)]
ROADS = [RING,[(0,0),(0,-1900),(0,-3100)],[(0,0),(2500,-350),(3550,-350)],
 [(0,0),(2200,1300),(3450,2550)],[(0,0),(-2200,1200),(-3300,2700)],
 [(0,0),(-2400,-400),(-3650,-650)],[(0,-1800),(-2100,-3000),(-3200,-3650)],
 [(2500,-1850),(3200,-2400),(3850,-3100)],[(0,1500),(-1600,2700),(-1600,3600)]]
DESERT = [(-4550,-700,2000),(-5500,500,1250)]
FOREST = [(4400,2900,1950),(3400,3800,1000)]
BLOSSOM = [(3000,2350,350)]
LAKE = (0,4700,2600,2050)
LAB_ISLAND = (-1800,4500,400)
OASIS = (-3650,500,200)
FROZEN_POND = (-2200,-4200,220)
# Rectangular surface boxes deliberately match the engine water boxes. Raised
# pools are queried before the ocean; all swimming/navigation uses these levels.
POOLS = [(1450,1950,-3250,-2200,220),(2200,2800,-2200,-800,60),
 (2550,3050,-800,1250,-90),(1550,2450,1250,2850,-180)]
ALLY_SPOTS = [(-2750,2700),(3100,2400),(-3350,550),(-2450,3200)]
CART_SPOTS = [(-2900,2600),(-2900,-2900),(3000,-2100),(3100,1900)]
ACCESS_SPOTS = [(0,-2800),(4300,-2650),(3600,-250),(3500,2500),(350,3900),(-3100,3000),(-3650,-650),(-3000,-3700)]
BOSS_SPOTS = [(-1450,-3050,0),(4300,-2750,1),(-2500,-3400,2),(5000,1900,3),(900,2500,4),(5300,-1450,5),(-5500,-50,6)]

# Catmull-Rom lanes preserve endpoints while softening long angular road chains.
def curved(points):
    pts=[points[0]]+points+[points[-1]];out=[]
    for i in range(1,len(pts)-2):
        a,b,c,d=pts[i-1:i+3]
        for k in range(8):
            t=k/8;out.append(tuple(.5*((2*b[j])+(-a[j]+c[j])*t+(2*a[j]-5*b[j]+4*c[j]-d[j])*t*t+(-a[j]+3*b[j]-3*c[j]+d[j])*t*t*t) for j in range(2)))
    return out+[points[-1]]
ROADS=[curved(p) for p in ROADS]
