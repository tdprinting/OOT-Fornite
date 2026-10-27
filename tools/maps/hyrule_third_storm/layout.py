"""Hyrule Before the Third Storm (map 15): the hand-set facts the exporter needs. The map itself is built in Blender in the map project
(C:/Users/mexic/Documents/Claude/zelda battle royale ultimate map/map, compact layout: map/compact/README.md).

Frames. The map project is in metres, origin at the south-west corner, +x east, +y north, +z up, 1,800 m square. The game is Y-up at
36 units a metre (adult Link, 68 units, is 1.89 m): game x = (x - 900) * 36, game y = z * 36, game z = -(y - 900) * 36 (north is -z, as in
OoT). The map spans -32400..32400 on x and z, inside the 16-bit world (shared/engine_limits.h kWorldCoordMax)."""

UNITS = 36.0
CENTRE = 900.0
HALF = 32400.0

def to_game(x, y, z):
    return ((x - CENTRE) * UNITS, z * UNITS, -(y - CENTRE) * UNITS)

# The 12 areas: id -> (prefix, name, compact anchor x, y). Anchors: map/compact/layout.py AREAS.
AREAS = {
    "01": ("CT", "Castle Town", 735, 1560),
    "02": ("KK", "Kakariko Village", 930, 1050),
    "03": ("GY", "Kakariko Graveyard", 1640, 1620),
    "04": ("DM", "Death Mountain", 1250, 1440),
    "05": ("ZD", "Zora's Domain", 1580, 1030),
    "06": ("KF", "Kokiri Forest", 1220, 210),
    "07": ("LW", "Lost Woods", 1610, 590),
    "08": ("LL", "Lon Lon Ranch", 670, 680),
    "09": ("LH", "Lake Hylia", 750, 240),
    "10": ("GV", "Gerudo Valley", 220, 610),
    "11": ("GF", "Gerudo Fortress", 230, 1130),
    "12": ("DC", "Desert Colossus", 230, 1600),
}
# Hyrule Field's 12 loot pockets (map/compact/field_tables.json POCKETS): name, x, y.
POCKETS = {
    "F-01": ("Old Abbey", 480, 1280), "F-02": ("Signal Ruin", 975, 759), "F-03": ("Southern Dairy Stop", 300, 230),
    "F-04": ("Ranch Crossroads", 665, 927), "F-05": ("Peahat Stones", 1151, 682), "F-06": ("River Bridge Camp", 1006, 604),
    "F-07": ("Orchard Shelter", 720, 1270), "F-08": ("Lake Outlet Mill", 1050, 470), "F-09": ("Gerudo Road Waystation", 460, 900),
    "F-10": ("Eastern Ford", 1270, 833), "F-11": ("Forest Edge Lookout", 1500, 300), "F-12": ("Desert Pass Refuge", 234, 1367),
}
# kRegions order: the 12 areas (Castle Town first: the hot drop, no guard), then the 12 pockets. 24 names (map.h kNamesPerMap).
REGION_ORDER = ["01", "02", "03", "04", "05", "06", "07", "08", "09", "10", "11", "12"] + sorted(POCKETS)
# The seven guarded places and their mini boss (shared/boss.h BossKind order: Stone 0, Lava 1, Frost 2, Moss 3, Tide 4, Shade 5, Dune 6;
# export.py checks the names against the header).
BOSSES = {"04": "Lava", "05": "Frost", "06": "Moss", "09": "Tide", "11": "Dune", "03": "Shade", "12": "Stone"}

# The lobby: the open Field between Kakariko and Lon Lon Ranch (compact metres). export.py moves it to the flattest dry spot within 40 m.
LOBBY = (800, 880)

# Draw chunks: the map is cut into CELL x CELL squares; a square near the camera draws its detail mesh, further off its far mesh.
CELL = 4096.0
NEAR = 7000.0          # detail out to this far from the camera (plus the square's own size): about 195 m
FAR_CLIP = 46000.0     # gMapFarClip for this map: the far mesh of the whole map is in sight from the middle
# The ground grid the server and the bots use (walk heights, open/blocked): GRID x GRID points, STEP units apart, covering +-32768.
GRID = 1025
STEP = 64.0
