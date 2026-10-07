"""Hyrule Kingdom: where everything is. Game units throughout (x east, y up, z south; north is -z, the top of the minimap).

The island blends the Fortnite island's structure (a snowy massif in the north, a warm desert in the south east, woods in the west, a lake town,
a dense clock-tower town in the middle) with the Hyrule maps: Hyrule Castle on its cliff above Zora's River, Lake Hylia fed by the river,
Death Mountain over Kakariko, Gerudo ruins in the desert, Lon Lon Ranch on its hill and Kokiri Forest under the big trees.
"""

# The island grid is the Fortnite Map's (shared/fortnite_map_data.h): 64 x 64 squares, the same size, one water level.
HALF_X = 7412.53
HALF_Z = 7705.72
CELLS = 64
SUB = 6
WATER_Y = -227
SEA_FLOOR = -560          # the open sea is perfectly flat here, so the exporter can replace it with one big square of collision

# name, x, z, radius of the place, boss kind (None, or the BossKind name), short description
POIS = [
    ("Hyrule Castle",      0,    -1650, 1050, "Stone", "the castle on its cliff: walls, gatehouse, towers and a keep you can climb"),
    ("Clock Town",         0,     1150,  950, None,    "a tight town of tall houses round a clock tower: rooftop fights"),
    ("Lake Hylia Stilts", -2950,  2350,  700, "Tide",  "a fishing village on stilts over the lake, joined by boardwalks"),
    ("Lakeside Lab",      -4150,  2550,  420, None,    "the lab and its tower on the island in the middle of the lake"),
    ("Gerudo Ruins",       4100,  3650,  950, "Dune",  "sandstone ruins and a broken colossus in the dunes"),
    ("Snowpeak Lodge",    -2600, -4700,  750, "Frost", "a big timber lodge on a snowy shelf below the peak"),
    ("Death Mountain",     4350, -4350,  800, "Lava",  "the volcano: a ring of rock round a smoking crater"),
    ("Kakariko Village",   4050, -1350,  850, "Shade", "houses, a windmill and a graveyard at the mountain's foot"),
    ("Kokiri Forest",     -5050, -1150,  850, "Moss",  "tree houses and stumps under giant trees"),
    ("Lon Lon Ranch",      1500,  4550,  850, None,    "a walled ranch on its hill: barn, silo, house and paddock"),
    ("Temple Ruins",      -1900,  4700,  650, None,    "the broken Temple of Time: columns, arches and a sealed door"),
    ("Ordon Docks",       -5000,  5150,  650, None,    "piers, boat sheds and a lighthouse where the lake meets the sea"),
    ("Zora's Falls",       2250, -5350,  600, None,    "the river's source: a waterfall into a deep pool"),
    ("Great Hylia Bridge", 1900, -2850,  450, None,    "an old stone bridge high over Zora's River"),
    ("Castle Bridge",         0,  -230,  350, None,    "the arched bridge between Clock Town and the castle"),
    ("Hyrule Field",       2600,   800, 1200, None,    "rolling meadows and lone oaks between everything"),
    ("Windmill Hill",      5000,  -500,  350, None,    "Kakariko's windmill on its rise"),
    ("Kakariko Graveyard", 5250, -2250,  400, None,    "headstones and a crypt under the cliffs"),
    ("Deku Tree Hollow",  -5750, -2300,  450, None,    "the giant old tree's clearing"),
    ("Blossom Oasis",      5400,  1350,  500, None,    "a pond ringed by pink blossom trees at the desert's edge"),
    ("Frozen Pond",       -1500, -5600,  400, None,    "an iced pool between the peaks"),
    ("Goron Lookout",      3350, -3700,  350, None,    "a stone lookout on Death Mountain's trail"),
    ("Fairy Fountain",    -3700,  -600,  350, None,    "a hidden fountain ring in the woods"),
    ("Lighthouse Point",  -5700,  5900,  300, None,    "the lighthouse at the end of the docks' spit"),
]
assert len(POIS) == 24

# The BossKind numbers (shared/boss.h order Stone, Lava, Frost, Moss, Tide, Shade, Dune)
BOSS_KINDS = {"Stone": 0, "Lava": 1, "Frost": 2, "Moss": 3, "Tide": 4, "Shade": 5, "Dune": 6}

# Zora's River, from the falls to Lake Hylia, and from the lake to the sea. (x, z, half width of the water)
RIVER = [
    (2250, -5150, 330), (2150, -4500, 300), (1950, -3700, 290), (1850, -2850, 290), (1650, -2050, 300), (1300, -1050, 300),
    (750, -380, 310), (0, -230, 320), (-800, -320, 310), (-1650, -150, 300), (-2350, 600, 300), (-2950, 1350, 330),
]
OUTLET = [(-4600, 3300, 300), (-5000, 3900, 320), (-5450, 4400, 360), (-6000, 4950, 420), (-6600, 5500, 520)]

LAKE = (-3950, 2350, 1750, 1350)        # centre and radii of Lake Hylia
LAB_ISLAND = (-4150, 2550, 360)          # the island in the lake
OASIS = (5400, 1350, 330)
FROZEN_POND = (-1500, -5600, 280)

# Flat ground for building on: x, z, radius of the flat top, height, how far the blend runs.
PADS = [
    (0, -1650, 900, 520, 330),        # castle plateau (its cliff is the blend)
    (0, 1150, 820, 40, 600),          # Clock Town
    (4050, -1350, 650, 150, 550),     # Kakariko
    (5000, -500, 220, 300, 420),      # Windmill Hill
    (5250, -2250, 330, 230, 380),     # graveyard
    (1500, 4550, 700, 230, 520),      # Lon Lon Ranch hill
    (-1900, 4700, 520, 60, 450),      # Temple Ruins
    (-2600, -4700, 520, 640, 480),    # Snowpeak Lodge shelf
    (4100, 3650, 600, 140, 600),      # Gerudo Ruins
    (-5050, -1150, 650, 80, 500),     # Kokiri Forest
    (-5750, -2300, 300, 120, 400),    # Deku Tree Hollow
    (3350, -3700, 200, 700, 380),     # Goron Lookout
    (-3700, -600, 220, 90, 300),      # Fairy Fountain
    (-5000, 5150, 380, -100, 350),    # Ordon Docks (just above the water)
    (-5700, 5900, 200, -40, 300),     # Lighthouse Point
    (5400, 1350, 600, 90, 450),       # Blossom Oasis rim
]

# Mountains: x, z, peak height, radius, crater (fraction of radius, 0 = none)
MOUNTAINS = [
    (4350, -4350, 1950, 2200, 0.22),   # Death Mountain
    (-2200, -5600, 1700, 2100, 0.0),   # Snowpeak (main)
    (-700, -6100, 1150, 1500, 0.0),    # Snowpeak (east shoulder)
    (-4000, -5500, 1050, 1600, 0.0),   # Snowpeak (west shoulder)
    (2700, -5700, 900, 1300, 0.0),     # the cliffs over Zora's Falls
]

# Dirt roads: chains of (x, z). They are painted on the ground and flattened a little.
ROADS = [
    [(0, 600), (0, -60)],                                                # Clock Town north gate to the Castle Bridge
    [(0, -420), (0, -800), (300, -1150)],                                 # bridge up to the castle gate
    [(800, 1150), (1800, 950), (2800, 400), (3500, -600), (4050, -1100)], # town east to Kakariko
    [(0, 2050), (300, 2900), (900, 3700), (1300, 4000)],                  # town south to Lon Lon
    [(-850, 1350), (-1700, 1650), (-2350, 2050)],                         # town west to the lake stilts
    [(-900, 1900), (-1300, 3100), (-1800, 4150)],                         # town to Temple Ruins
    [(2100, 4600), (3000, 4200), (3550, 3850)],                           # Lon Lon to Gerudo
    [(4300, -1900), (3900, -2700), (3400, -3500)],                        # Kakariko up the trail to Goron Lookout
    [(3400, -3500), (3700, -3900), (3900, -4150)],                        # trail to the crater rim
    [(4600, -1000), (5000, -650)],                                        # Kakariko to the windmill
    [(4550, -1700), (5050, -2050)],                                       # Kakariko to the graveyard
    [(-1800, 400), (-3000, -300), (-4400, -900)],                         # field to Kokiri
    [(-1800, 400), (-850, 1350)],
    [(-2300, -1100), (-2700, -2500), (-2900, -4000)],                     # forest edge up to Snowpeak Lodge
    [(-4300, 3700), (-4700, 4700), (-5000, 5100)],                                       # lake to the docks
    [(1100, -2000), (1900, -2850), (2600, -3400)],                        # castle back road over the Great Bridge
    [(3500, 1700), (4600, 1500), (5050, 1350)],                           # field to Blossom Oasis
    [(2800, 400), (3500, 1700), (3900, 2900)],                            # field down to the desert
]

# Biome regions (x, z, radius): the strongest one wins where they overlap; the rest is Hyrule Field meadow.
DESERT = [(4200, 3700, 2600), (5600, 2600, 1700), (2800, 5200, 1500)]
FOREST = [(-5100, -1100, 1900), (-4600, 400, 1300), (-5600, -2600, 1300), (-3700, -1500, 900)]
BLOSSOM = [(5400, 1350, 950), (6000, 200, 700)]
