"""Skyloft Isles: where everything is. Game units throughout (x east, y up, z south; north is -z, the top of the minimap).

Layout inspired by Skyloft (Skyward Sword), rebuilt as original low-poly geometry in the project's OoT look, with open sea instead of the
cloud sea. One main island of lobes round a central plateau: the Knight Academy on its own high lobe in the north west, joined by two
bridges; the Goddess Statue on the hill in the north east; the Bazaar's great tent in the middle (the hot drop); the Statue Plaza and the
diving decks south of it; pumpkin fields east; the Light Tower on the south headland; cliffside homes west; the graveyard and the Waterfall
Cave on the east cliffs. Islets in the sea: Beedle's Perch (west), the Lumpy Pumpkin (south east, by a long bridge), Ember Rock (south, by
a bridge from the Light Tower) and the Loftwing Roost crag (north east, by a rope bridge from the statue hill).

Scale: adult Link is 68 units tall (1 m = 40 units); the kit (kit.py) is Moonfall's. The island is about 11,000 x 9,700 across.
"""

# The island grid is the Fortnite Map's (shared/fortnite_map_data.h): 64 x 64 squares, the same size, one sea level.
HALF_X = 7412.53
HALF_Z = 7705.72
CELLS = 64
SUB = 6
WATER_Y = -227
SEA_FLOOR = -560          # the open sea is perfectly flat here, so the exporter can replace it with one big square of collision
MAP_NAME = 'skyloft'
MAP_TITLE = 'Skyloft Isles'
MAP_INDEX = 13
ASSET_DIR = 'skyloft_isles'

# Height levels (the island is terraced so every level reads from afar)
PLATEAU = 100       # the central plateau: Bazaar, plaza, fields, homes
TERRACE = 380       # the academy lobe's lower terrace: the Sparring Hall and the courtyard
ACADEMY = 520       # the Knight Academy's own shelf
STATUE = 560        # the top of the statue hill
CLIFFS = 400        # the east cliff ridge over the Waterfall Cave
HEADLAND = 60       # the south headland under the Light Tower
ROOST = 640         # the Loftwing Roost crag

# name, x, z, radius of the place, boss kind (None, or the BossKind name), short description
POIS = [
    ("Knight Academy",     -4200, -3900,  900, None,    "the red-roofed academy on its high lobe: dormitories, the headmaster's tower and the courtyard"),
    ("Bazaar",              -700,  -400,  800, None,    "the great striped tent in the middle of the plateau, stalls all round: the hot drop"),
    ("Goddess Statue",      2800, -1800, 1000, "Dune",  "the stone goddess on her hill, her plinth a shrine, a stair road winding up"),
    ("Sparring Hall",      -3100, -2600,  600, "Stone", "the knights' training hall and yard on the academy terrace"),
    ("Waterfall Cave",      4200, -1500,  650, "Tide",  "the gorge in the east cliffs: a waterfall into a pool and a roofless cave behind it"),
    ("Lumpy Pumpkin",       5700,  4100,  600, None,    "the pumpkin-topped inn on its own islet, reached by the long bridge"),
    ("Light Tower",         1600,  4000,  500, None,    "the lighthouse on the south headland: the tallest thing on the isle"),
    ("Pumpkin Fields",      1700,   300,  750, "Moss",  "fenced pumpkin patches, a barn and a windmill east of the Bazaar"),
    ("Westwind Homes",     -3700,  2700,  750, None,    "cliffside cottages and gardens on the west lobe"),
    ("Hollow Graveyard",    3700,  2900,  600, "Shade", "headstones round the old crypt on the east lobe"),
    ("Loftwing Roost",      4400, -3600,  500, "Frost", "the bare crag in the north east with the great birds' nests"),
    ("Ember Rock",          1700,  5600,  420, "Lava",  "a black rock islet with the scrap smithy, over the south bridge"),
    ("Statue Plaza",        -900,  1700,  600, None,    "the paved plaza with the bird statue, benches and lamps"),
    ("Academy Bridge",     -2600, -2000,  350, None,    "the stone bridge from the plateau up to the academy terrace"),
    ("Goddess Stair",       1700, -1300,  350, None,    "the switchback stair road up the statue hill"),
    ("Beedle's Perch",     -5500,  1300,  400, None,    "the little islet with the merchant's tower, off the west shore"),
    ("Pumpkin Bridge",      4900,  3700,  350, None,    "the long timber bridge out to the Lumpy Pumpkin"),
    ("West Diving Deck",   -1700,  3000,  300, None,    "a wooden launch deck over the west sea"),
    ("South Diving Deck",       0,  3650,  300, None,    "a wooden launch deck over the south sea"),
    ("Waterfall Pool",      3800,  -500,  380, None,    "the pool at the foot of the falls, with a jetty"),
    ("Windmill Hill",       -300, -2000,  400, None,    "the north windmill on its knoll over the Bazaar"),
    ("East Span",           2600,  2000,  350, None,    "the rope-and-plank bridge over the east gully to the graveyard lobe"),
    ("Fledge's Cove",      -3000,  4000,  400, None,    "a boat cove with a dock under the west homes"),
    ("Ember Bridge",        1650,  4800,  300, None,    "the bridge from the Light Tower headland to Ember Rock"),
]
assert len(POIS) == 24
POI_AT = {name: (x, z) for name, x, z, *_ in POIS}

# The BossKind numbers (shared/boss.h order Stone, Lava, Frost, Moss, Tide, Shade, Dune)
BOSS_KINDS = {"Stone": 0, "Lava": 1, "Frost": 2, "Moss": 3, "Tide": 4, "Shade": 5, "Dune": 6}

# Land: circles of (x, z, radius) whose union (with a little noise) is the coast. The academy lobe is its own island.
MAIN_LOBES = [
    (0, 500, 3600),            # the central plateau
    (2900, -1700, 2100),       # the statue hill
    (3900, 2500, 1300),        # the east lobe (graveyard)
    (-3600, 2500, 1500),       # the west lobe (homes)
    (1500, 3900, 1050),        # the south headland (Light Tower)
    (3900, -300, 1200),        # the east cliffs (Waterfall Cave)
    (-300, -2000, 1100),       # Windmill Hill, north of the Bazaar
]
ACADEMY_LOBE = [(-4200, -3700, 1750), (-3200, -2700, 1000)]
ISLETS = [
    (-5500, 1300, 400, 40),      # Beedle's Perch: x, z, radius, height
    (5700, 4100, 600, 60),       # the Lumpy Pumpkin
    (1700, 5600, 520, 80),       # Ember Rock
    (4400, -3600, 620, ROOST),   # the Loftwing Roost
]
# Sea channels: the east gully between the plateau and the graveyard lobe (crossed by the East Span), and the academy channel between the
# plateau and the academy lobe (crossed by the Academy Bridge): chains of (x, z, half width of the water)
GULLY = [(2600, 1500, 260), (2650, 2000, 260), (2800, 2700, 280), (3100, 3600, 320)]
CHANNEL = [(-3500, -700, 340), (-2600, -1700, 300), (-2200, -2500, 300), (-1700, -3400, 340), (-1400, -4300, 420)]
# The waterfall gorge: cut into the east cliffs from the pool at its foot
GORGE = [(3800, -500, 230), (4100, -1100, 200), (4250, -1550, 180)]

# Raised water (its own box in the game): x0, x1, z0, z1, y. The reservoir on the cliff top feeds the waterfall.
POOLS = [
    (4050, 4450, -2050, -1750, CLIFFS - 30),   # the cliff-top reservoir behind the falls
    (-5000, -4600, -4300, -3950, ACADEMY - 30),   # the academy's pond
]

# Flat ground for building on: x, z, radius of the flat top, height, how far the blend runs.
PADS = [
    (-700, -400, 900, PLATEAU, 500),          # Bazaar
    (-900, 1700, 700, PLATEAU - 10, 400),      # Statue Plaza
    (1700, 300, 850, PLATEAU + 10, 450),       # Pumpkin Fields
    (-3700, 2700, 800, PLATEAU + 30, 450),     # Westwind Homes
    (3700, 2900, 700, PLATEAU + 40, 400),      # Hollow Graveyard
    (1600, 4000, 560, HEADLAND, 380),          # Light Tower headland
    (-300, -2000, 420, PLATEAU + 150, 500),    # Windmill Hill knoll
    (2800, -1800, 520, STATUE, 900),           # the statue's hill top
    (-4200, -3900, 950, ACADEMY, 420),         # the academy shelf
    (-3100, -2600, 820, TERRACE, 420),         # the Sparring Hall terrace
    (4000, -1900, 700, CLIFFS, 500),           # the east cliff top (reservoir)
    (3800, -500, 380, 40, 300),                # the waterfall pool's floor shelf (water box is the sea)
    (-5500, 1300, 300, 40, 200),               # Beedle's Perch
    (5700, 4100, 440, 60, 200),                # the Lumpy Pumpkin islet
    (1700, 5600, 380, 80, 200),                # Ember Rock
    (4400, -3600, 380, ROOST, 300),            # the Loftwing Roost top
    (-1700, 3000, 320, PLATEAU - 20, 200),     # West Diving Deck landing
    (0, 3650, 320, PLATEAU - 30, 200),         # South Diving Deck landing
    (-2150, -1550, 260, PLATEAU, 200),         # Academy Bridge, plateau end
    (-2950, -2350, 240, TERRACE, 160),         # Academy Bridge, terrace end
    (1650, 4450, 220, HEADLAND, 160),          # Ember Bridge, headland end
    (1690, 5260, 220, 80, 160),                # Ember Bridge, islet end
    (2240, 1930, 220, PLATEAU, 160),           # East Span, west end
    (3020, 2160, 220, PLATEAU + 40, 160),      # East Span, east end
    (4700, 3280, 220, PLATEAU + 40, 160),      # Pumpkin Bridge, lobe end
    (5350, 3810, 220, 60, 160),                # Pumpkin Bridge, islet end
    (-4740, 1640, 200, PLATEAU + 30, 160),     # Beedle's bridge, lobe end
    (-5260, 1460, 200, 40, 140),               # Beedle's bridge, islet end
    (-3000, 4000, 300, -60, 300),              # Fledge's Cove beach
]

# Dirt roads: chains of (x, z). They are painted on the ground and flattened a little.
ROADS = [
    [(-700, -400), (-800, 600), (-900, 1700)],                               # Bazaar south to the plaza
    [(-900, 1700), (-1400, 2500), (-1700, 3000)],                            # plaza to the West Diving Deck
    [(-900, 1700), (-500, 2800), (0, 3650)],                                 # plaza to the South Diving Deck
    [(-900, 1700), (200, 2500), (1000, 3200), (1600, 4000)],                 # plaza to the Light Tower
    [(-700, -400), (500, -100), (1700, 300)],                                # Bazaar east to the fields
    [(1700, 300), (2200, 1000), (2600, 2000)],                               # fields to the East Span
    [(2600, 2000), (3100, 2500), (3700, 2900)],                              # East Span to the graveyard
    [(3700, 2900), (4300, 3300), (4900, 3700), (5700, 4100)],                 # graveyard over the Pumpkin Bridge to the inn
    [(1700, 300), (2700, 100), (3800, -500)],                                # fields to the Waterfall Pool
    [(-700, -400), (300, -900), (1200, -1200), (1700, -1300)],               # Bazaar to the Goddess Stair's foot
    [(-700, -400), (-500, -1300), (-300, -2000)],                            # Bazaar north to Windmill Hill
    [(-700, -400), (-1600, -1100), (-2200, -1600)],                          # Bazaar north west to the Academy Bridge's foot
    [(-2950, -2350), (-3100, -2600), (-3600, -3300), (-4200, -3900)],        # terrace up to the academy
    [(-1400, 2500), (-2600, 2300), (-3700, 2700)],                           # plaza road west to the homes
    [(-3700, 2700), (-3200, 3400), (-3000, 4000)],                           # homes down to Fledge's Cove
    [(-3700, 2700), (-4300, 2000), (-4750, 1620)],                           # homes to the Beedle's Perch landing
]

# Where the seven hireable allies wait, in shared/ally.h order (the server puts ally kind i at station i): Kokiri, Zora, Goron, Gerudo,
# Jones, Peter, Master Chief.
ALLY_SPOTS = [
    (1900, 700, 0),        # Kokiri at the pumpkin fields' barn
    (3750, -250, 1),       # Zora at the Waterfall Pool's jetty
    (1550, 5350, 2),       # Goron at the Ember Rock smithy
    (2650, -1500, 3),      # Gerudo at the Goddess Stair's top
    (-2900, -2300, 4),     # Jones at the Academy Bridge's terrace end
    (5450, 4000, 5),       # Peter at the Lumpy Pumpkin's door
    (1350, 3850, 6),       # Master Chief at the Light Tower
]
# Carts (the game's buggies) stand where the roads meet.
CART_SPOTS = [(-800, 700), (600, -100), (2200, 1000), (-1800, -1300), (-2800, 2350), (3300, 2600)]
SPAWN = (-900, 900)   # the lobby: the flat paving between the Bazaar and the plaza
