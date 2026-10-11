"""Moonfall Isle: where everything is. Game units throughout (x east, y up, z south; north is -z, the top of the minimap).

The island follows Tee's reference painting: snow peaks and a lodge in the north west, sandstone ruins and a seated colossus in the west,
Hyrule Castle on its plateau in the north with waterfalls off its cliff, a volcano fort in the north east, Kakariko's windmill village in the
east, the Temple of Time's ruins in the central meadow, Romani Ranch in the south west, a turquoise lagoon with a stilt village and piers
in the south, the Deku Tree's village in the south east, and stone bridges, waterfalls and dirt roads joining them. Clock Town (Majora's
Mask) is the walled hot-drop town east of the centre.

Scale: adult Link is 68 units tall (1 m = 40 units). Everything in kit.py is sized from that (see kit.SCALE).
"""

# The island grid is the Fortnite Map's (shared/fortnite_map_data.h): 64 x 64 squares, the same size, one sea level.
HALF_X = 7412.53
HALF_Z = 7705.72
CELLS = 64
SUB = 6
WATER_Y = -227
SEA_FLOOR = -560          # the open sea is perfectly flat here, so the exporter can replace it with one big square of collision
MAP_NAME = 'moonfall'
MAP_TITLE = 'Moonfall Isle'
MAP_INDEX = 12

# name, x, z, radius of the place, boss kind (None, or the BossKind name), short description
POIS = [
    ("Hyrule Castle",       200, -4600, 1000, "Stone", "the white castle on its plateau: walls, gatehouse, towers, a keep and waterfalls off the cliff"),
    ("Clock Town",         2900,  -400,  900, None,    "the walled town round the clock tower: rooftops, stalls and the Milk Bar"),
    ("Temple of Time",     -200,   100,  750, None,    "the broken temple in the middle of the meadow: the hot drop"),
    ("Snowhead Lodge",    -3700, -4300,  800, "Frost", "a timber lodge and cabins on the snowy shelf under the peaks"),
    ("Spirit Colossus",   -4900, -2200,  950, "Dune",  "sandstone ruins round the seated stone giant"),
    ("Goron Forge",        3900, -3900,  850, "Lava",  "the red-stone fort on the volcano's shelf, over the lava, with its lookout tower"),
    ("Kakariko Windmill",  4600, -2100,  850, None,    "red-roofed houses round the windmill at the mountain's foot"),
    ("Ikana Graveyard",    4800, -3050,  380, "Shade", "headstones and the crypt in Kakariko's north yard"),
    ("Romani Ranch",      -4600,  1300,  850, None,    "a fenced ranch: barn, silo, house and paddocks"),
    ("Great Bay Stilts",   -300,  3800,  700, "Tide",  "a fishing village on stilts in the lagoon, joined by boardwalks"),
    ("Deku Tree Village",  4300,  2900,  900, "Moss",  "the hollow giant tree with decks and huts in its branches"),
    ("Lost Woods",         2700,  1700,  550, None,    "a dark maze of great oaks on the way to the Deku Tree"),
    ("Astral Observatory", -2600,  -900,  350, None,    "the stargazer's tower on its knoll"),
    ("Zora Cape",         -4000,  3900,  600, None,    "the lighthouse, piers and boat sheds facing the lagoon"),
    ("Castle Falls",       1400, -3450,  420, None,    "the pool at the foot of the castle's waterfall"),
    ("Moonfall Span",     -1700, -4400,  350, None,    "the high stone bridge from the castle's west road to the Snowhead shelf"),
    ("Milk Road",         -2550,  1900,  350, None,    "the ranch road's bridge over the west river"),
    ("Fairy Fountain",    -2200, -3200,  300, None,    "a hidden fountain ring in the pines"),
    ("Termina Bridge",     1450,  2300,  400, None,    "the stone bridge over the east river's mouth"),
    ("Lantern Bridge",     1300,  -500,  300, None,    "the lantern-lit bridge from the meadow to Clock Town's gate"),
    ("Moon Crater",       -1600,  2200,  400, None,    "the cracked crater where the moon fell: its chests are the richest on the isle"),
    ("Frozen Pond",       -2300, -5000,  330, None,    "an iced pool between the peaks with a fishing hut"),
    ("Spirit Oasis",      -5600,  -600,  400, None,    "palms and a pavilion round a pond at the desert's edge"),
    ("Fisherman's Hut",    1000,  4800,  300, None,    "a hut on its own little island in the lagoon"),
]
assert len(POIS) == 24
POI_AT = {name: (x, z) for name, x, z, *_ in POIS}

# The BossKind numbers (shared/boss.h order Stone, Lava, Frost, Moss, Tide, Shade, Dune)
BOSS_KINDS = {"Stone": 0, "Lava": 1, "Frost": 2, "Moss": 3, "Tide": 4, "Shade": 5, "Dune": 6}

# Rivers at sea level in gorges: (x, z, half width of the water). The east river runs from the castle's waterfall pool south through the
# meadow to the lagoon; the west river drains Lake Romani into the lagoon.
EAST_RIVER = [(1420, -3200, 240), (1550, -2700, 260), (1700, -2000, 260), (1600, -1300, 260), (1300, -600, 270),
              (1100, 200, 270), (1150, 1000, 280), (1400, 1800, 290), (1450, 2500, 320), (1200, 3100, 420)]
WEST_RIVER = [(-3000, 700, 300), (-2850, 1100, 260), (-2650, 1600, 250), (-2500, 2300, 270), (-2250, 2900, 360), (-1900, 3400, 520)]
RIVERS = [EAST_RIVER, WEST_RIVER]

LAKE_ROMANI = (-3300, 200, 820, 560)      # centre and radii of the lake above the ranch
LAGOON = (-300, 4500, 2500, 1800)         # the turquoise lagoon: centre and radii; it opens to the sea in the south
ZORA_BAY = (-3200, 4700, 1500, 1000)      # the bay west of the lagoon that Zora Cape stands in
FISH_ISLAND = (1000, 4800, 320)
OASIS = (-5600, -600, 300)
FROZEN_POND = (-2500, -5400, 260)
FALLS_POOL = (1400, -3450, 360)           # the pool at the foot of the castle's waterfall (sea level, the river's head)

# Raised water (its own box in the game): x0, x1, z0, z1, y. The castle's garden lake feeds the waterfall.
POOLS = [
    ( 400,  900, -4250, -3900, 575),       # castle garden lake on the plateau's south east edge, over the falls
    (-3780, -3420, 1120, 1480, 118),      # the ranch's pond
    (-5900, -5300, -900, -300, 60),       # Spirit Oasis pond
]

# Flat ground for building on: x, z, radius of the flat top, height, how far the blend runs.
PADS = [
    (200, -4600, 1050, 620, 420),      # castle plateau (its cliff is the blend)
    (2900, -400, 1000, 60, 420),       # Clock Town
    (4600, -2100, 950, 200, 420),      # Kakariko
    (-2600, -900, 480, 240, 620),      # Astral Observatory knoll (gentle enough to walk up all round)
    (-4600, 1300, 880, 150, 440),      # Romani Ranch
    (-200, 100, 700, 95, 450),         # Temple of Time
    (-3700, -4300, 850, 520, 700),     # Snowhead shelf
    (-2300, -5000, 400, 700, 360),     # Frozen Pond shelf
    (3900, -3900, 900, 540, 600),      # Goron Forge shelf on the volcano
    (4800, -3050, 460, 260, 300),      # Ikana graveyard (after the forge shelf, whose slope it is cut into)
    (-2550, -4450, 340, 560, 260),     # the span's west abutment ledge
    (-900, -4450, 320, 580, 260),      # the span's castle-side ledge
    (-4900, -2200, 900, 160, 500),     # Spirit Colossus
    (-5600, -600, 560, 90, 380),       # Spirit Oasis rim
    (4300, 2900, 820, 110, 420),       # Deku Tree Village
    (2700, 1700, 520, 120, 360),       # Lost Woods
    (-1600, 2200, 420, 70, 320),       # Moon Crater rim
    (-2200, -3200, 300, 200, 400),     # Fairy Fountain
    (-4000, 3900, 620, -60, 360),      # Zora Cape (just above the water)
    (1000, 4800, 300, -80, 260),       # Fisherman's island
]

# Mountains: x, z, peak height, radius, crater (fraction of radius, 0 = none)
MOUNTAINS = [
    (4700, -5000, 2000, 2200, 0.24),   # Death Mountain, the volcano
    (-4300, -5800, 1800, 2100, 0.0),   # Snowhead (main)
    (-2300, -6300, 1250, 1500, 0.0),   # Snowhead (east shoulder)
    (-5900, -4300, 1100, 1500, 0.0),   # Snowhead (west shoulder)
    (900, -6500, 1150, 1500, 0.0),     # the crags behind the castle
    (6400, -4700, 900, 1300, 0.0),     # the volcano's eastern spur over Ikana
    (-6200, 3000, 500, 1200, 0.0),     # the cape's hills
    (6300, 3200, 650, 1300, 0.0),      # the cliffs behind the Deku Tree
]

# Dirt roads: chains of (x, z). They are painted on the ground and flattened a little.
ROADS = [
    [(200, -3550), (600, -3350), (1000, -3100), (700, -2800), (200, -2550), (-200, -2100), (-200, -1200), (-200, -650)],   # the castle's switchback road down to the temple
    [(-200, -650), (-200, 850), (-200, 1400)],                                   # through the temple to the lagoon road
    [(400, -100), (900, -300), (1300, -500)],                                    # temple to Lantern Bridge
    [(1300, -500), (1800, -450), (2300, -400), (2900, -400)],                    # Lantern Bridge into Clock Town's west gate
    [(2900, -400), (3300, -900), (3900, -1600), (4600, -2100)],                  # Clock Town north east to Kakariko
    [(4600, -2100), (4800, -2600), (4800, -3050)],                               # Kakariko to the graveyard
    [(4600, -2100), (4100, -2800), (3800, -3400), (3900, -3900)],                # Kakariko up the trail to the forge
    [(2900, -400), (2900, 600), (2700, 1700)],                                   # Clock Town south into the Lost Woods
    [(2700, 1700), (3400, 2300), (4300, 2900)],                                  # Lost Woods to the Deku Tree
    [(2700, 1700), (2000, 2100), (1450, 2300)],                                  # Lost Woods to Termina Bridge
    [(1450, 2300), (600, 2600), (-300, 2900)],                                   # Termina Bridge along the lagoon's north shore to the stilts' landing
    [(-300, 2900), (-1100, 2600), (-1600, 1800), (-2550, 1900)],                 # north shore west past the crater to Milk Road
    [(-2550, 1900), (-3300, 1600), (-4600, 1300)],                               # Milk Road to the ranch
    [(-4600, 1300), (-4500, 2500), (-4000, 3900)],                               # ranch down to Zora Cape
    [(-200, 1400), (-900, 2000), (-1600, 1800)],                                 # temple south west to the crater
    [(-700, -200), (-1600, -500), (-2600, -900), (-3800, -300)],                 # temple west past the observatory to the lake
    [(-3800, -300), (-4200, 500), (-4600, 1300)],
    [(-2600, -900), (-3600, -1500), (-4900, -2200)],                             # observatory to the colossus
    [(-4900, -2200), (-5300, -1400), (-5600, -600)],                             # colossus to the oasis
    [(-1600, -500), (-1900, -1900), (-2200, -3200), (-2600, -3900), (-3700, -4300)],   # the pine road up to Snowhead
    [(-3700, -4300), (-3100, -4700), (-2300, -5000)],                            # lodge to the Frozen Pond
    [(-650, -4450), (-1250, -4450), (-2150, -4450), (-2700, -4350), (-3300, -4300)],   # the castle's west road over Moonfall Span to the lodge
    [(200, -3550), (-300, -3900), (-650, -4450)],                                # castle gate to the west road
]

# Biome regions (x, z, radius): the strongest one wins where they overlap; the rest is Hyrule Field meadow.
DESERT = [(-5200, -2000, 2300), (-6000, -400, 1500), (-5400, -3600, 1100)]
FOREST = [(4300, 2900, 1800), (2700, 1700, 1100), (5300, 4000, 1200), (-2200, -3300, 900), (-1300, -4500, 900), (-5300, 4800, 900)]
BLOSSOM = [(-2600, -900, 650)]

# Where the four kinds of hireable allies wait (shared/ally.h kinds: 0 Kokiri, 1 Zora, 2 Goron, 3 Gerudo): x, z, kind.
ALLY_SPOTS = [
    (4000, 3200, 0), (2500, 1400, 0),          # Kokiri at the Deku Tree and in the Lost Woods
    (-700, 3500, 1), (-3600, 3700, 1),         # Zora at the stilts and at Zora Cape
    (3600, -3700, 2), (-3300, -4100, 2),       # Goron at the forge and at Snowhead (the Goron village of Majora's Mask)
    (-4500, -2500, 3), (-5400, -800, 3),       # Gerudo at the colossus and the oasis
]
# Carts (the game's buggies) stand where the roads meet.
CART_SPOTS = [(-4300, 1900), (3450, 150), (4000, -1700), (300, -650), (-1200, -1200), (200, -3250)]
SPAWN = (300, -900)   # the lobby's flat meadow between the temple and the castle road
