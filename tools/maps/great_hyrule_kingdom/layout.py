"""Great Hyrule Kingdom: where everything is. Game units throughout (x east, y up, z south; north is -z, the top of the minimap).

The ground is Tee's fantasy landscape (prepare_landscape.py, terrain.py): a broad island that climbs from a lagoon in the north, over farmland
and a central plateau, to sandy uplands in the west, snow peaks in the south west and Death Mountain's red rock in the south east, with a
river in a valley down its eastern side. The map names it as one big Hyrule (Ocarina of Time and Twilight Princess places, the Zelda names
in the stylised 3D look): Castle Town in the middle (the hot drop) under Hyrule Castle, Lake Hylia's isle in the lagoon, Ordon and Lon Lon
in the farmland, Kokiri Forest and the Lost Woods in the north east, the river's Zora places, Kakariko on the east ridge, the Gerudo and the
Colossus in the west, Snowpeak in the south west and Goron City under Death Mountain.

Zero build: every place has rooftops, upper floors, walls and rocks to fight from; the open ground between places is broken by ruins,
rocks, hedges and trees so no run is across bare grass for long; roads and bridges join everything; nothing is out of a bot's reach.

Scale: adult Link is 68 units tall (1 m = 40 units); the kit (kit.py) is Moonfall's.
"""

# The island grid is the Fortnite Map's (shared/fortnite_map_data.h): 64 x 64 squares, the same size, one sea level.
HALF_X = 7412.53
HALF_Z = 7705.72
CELLS = 64
SUB = 6
WATER_Y = -227
SEA_FLOOR = -560          # the open sea is perfectly flat here, so the exporter can replace it with one big square of collision
MAP_NAME = 'great_hyrule_kingdom'
MAP_TITLE = 'Great Hyrule Kingdom'
MAP_INDEX = 14
ASSET_DIR = 'great_hyrule_kingdom'

# How the landscape sits in the grid: scaled about the centre so the sea goes all round, its heights mapped so the model's lagoon water
# (model height MODEL_SEA) is the sea level.
LAND_SCALE = 0.93
LAND_HALF_X = 7415.0
LAND_HALF_Z = 7772.0
MODEL_SEA = 70.0
VERTICAL = 1.0
COAST_SLOPE = 0.6        # the shore: the land may rise at most this much per unit inland from the water's edge
SNOW_LINE = 520          # in the snow region, snow lies above this
PEAK_SNOW = 1150         # anywhere, the very tops are white

# Lagoon fixes: extra water circles (x, z, r) where the model's water reads as land, and land kept where it reads as water (x, z, r).
LAGOON_EXTRA = [(-1300, -5000, 700), (400, -5200, 700)]
LAND_KEEP = [(-400, -5400, 480)]

# name, x, z, radius of the place, boss kind (None, or the BossKind name), short description
POIS = [
    ("Castle Town",         -100,  -800,  800, None,    "the walled market town in the middle of the plateau: rooftops, stalls, the fountain square: the hot drop"),
    ("Hyrule Castle",       -300,  1250,  750, "Stone", "the white castle on the plateau's high southern rise: curtain wall, gatehouse, towers and the keep"),
    ("Lake Hylia",          -400, -5400,  480, "Tide",  "the isle in the lagoon: the lakeside lab, a watchtower and piers, reached by the long bridge"),
    ("Hylia Docks",        -1900, -3500,  500, None,    "boat sheds, piers and a crane on the lagoon's south west shore"),
    ("Ordon Village",      -3700, -3300,  800, None,    "a farming village of thatched houses, a pumpkin patch and a goat barn by the lagoon"),
    ("Lon Lon Ranch",      -3900, -1200,  750, None,    "a fenced ranch: barn, silo, the house and the horse paddock"),
    ("Kokiri Forest",       3000, -4300,  850, "Moss",  "tree houses on decks round the Deku Tree, deep in the north east wood"),
    ("Lost Woods",          3800, -2700,  550, None,    "a maze of great dark oaks and hollow logs south of the Kokiri"),
    ("Great Hylia Bridge",  1700, -2900,  420, None,    "the long stone bridge over the river where it falls into the lagoon"),
    ("Zora's River",        2500,  -900,  450, None,    "the river's rapids and waterfalls, a wooden mill and a rope bridge"),
    ("Zora's Domain",       1200,  1800,  600, None,    "the river's head: a blue lake with the Zora's shrine and ice-blue pavilions"),
    ("Kakariko Village",    3900,   300,  850, None,    "red-roofed houses round the well and the windmill on the east ridge"),
    ("Kakariko Graveyard",  4400, -1400,  420, "Shade", "headstones and the royal tomb in Kakariko's north yard"),
    ("Hyrule Field Ruins", -2000,  1500,  550, None,    "broken walls and an old watchtower in the plateau's west meadow"),
    ("Gerudo Fortress",    -4800,  1400,  850, "Dune",  "the sandstone fortress on its mesa: walls, towers and the training yard"),
    ("Haunted Wasteland",  -5100,  -300,  450, None,    "sand and the poe's broken flag posts at the desert's northern edge"),
    ("Desert Colossus",    -3300,  2800,  700, None,    "sandstone ruins round the seated stone giant"),
    ("Temple of Time",       500,  2900,  700, None,    "the broken temple on the round green mesa south of the castle"),
    ("Snowpeak Lodge",     -4100,  4500,  800, "Frost", "a timber lodge and cabins on the snowy shelf under the peaks"),
    ("Snowpeak Summit",    -2300,  5300,  400, None,    "the icy look-out on the shoulder of Snowpeak, with a frozen pond"),
    ("Lanayru Spring",       300,  5100,  500, None,    "the spring and its shrine in the valley between the snow and the fire"),
    ("Goron City",          3600,  3100,  850, "Lava",  "the Gorons' red-stone halls cut into Death Mountain's lower shelf"),
    ("Death Mountain",      4900,  5000,  500, None,    "the volcano's crater rim: ash, lava and the richest chests on the map"),
    ("Death Mountain Trail", 4900, 1700,  450, None,    "the switchback trail from Kakariko up towards the Gorons, with a rest hut"),
]
assert len(POIS) == 24
POI_AT = {name: (x, z) for name, x, z, *_ in POIS}

# The BossKind numbers (shared/boss.h order Stone, Lava, Frost, Moss, Tide, Shade, Dune)
BOSS_KINDS = {"Stone": 0, "Lava": 1, "Frost": 2, "Moss": 3, "Tide": 4, "Shade": 5, "Dune": 6}

# Mountains added to the landscape (its own south is high ground, but gentle): x, z, extra height, radius, crater (fraction, 0 none)
MOUNTAINS = [
    (4900, 5000, 1300, 1800, 0.22),    # Death Mountain
    (-4700, 5600, 1100, 1500, 0.0),    # Snowpeak
    (-2700, 6100, 600, 1200, 0.0),      # Snowpeak's eastern shoulder
    (-6100, 4300, 550, 1100, 0.0),      # Snowpeak's western shoulder
    (-6000, 2400, 260, 1000, 0.0),     # the desert's high west rim
    (5800, -300, 260, 1100, 0.0),      # the ridge behind Kakariko
    (2600, 5800, 300, 1100, 0.0),      # the ash hills west of the volcano
]

# The river: (x, z, half width, water level). It rises in Zora's Domain's lake and falls north through pools to the lagoon; each segment
# from point k to k+1 is one flat pool at point k's level, so the steps are waterfalls. The last point is in the lagoon (sea level).
RIVER = [
    (1300, 1500, 220, 330),
    (1800,  900, 200, 160),     # a 170 fall into the gorge below the Domain
    (2300,  200, 200, 90),
    (2500, -700, 210, 20),
    (2400, -1600, 220, -60),
    (2100, -2500, 230, -140),
    (1650, -3300, 260, -227),
    (1300, -4100, 300, -227),
]
# Raised water (its own box in the game): x0, x1, z0, z1, y. Zora's lake, the river's pools (one per stepped segment, filled in below),
# Lanayru's spring and the frozen pond on the summit.
POOLS = [
    (700, 1700, 1500, 2200, 380),        # Zora's Domain lake
    (60, 560, 4900, 5300, 480),          # Lanayru Spring
]
def _river_pools():
    out = []
    for (ax, az, aw, lv), (bx, bz, bw, _) in zip(RIVER[:-1], RIVER[1:]):
        if lv <= WATER_Y + 1: continue
        w = max(aw, bw) + 20
        out.append((min(ax, bx) - w, max(ax, bx) + w, min(az, bz), max(az, bz), lv))
    return out
RIVER_POOLS = _river_pools()

# Mountain passes: ((x, z) start, (x, z) end, half width of the flat floor). The ground between is cut to a straight slope.
CUTS = [
    ((3900, 3450), (4760, 4800), 300),     # the Death Mountain pass: from Goron City's shelf through the crater rim to the crater floor
]

# Flat ground for building on: x, z, radius of the flat top, height (None: the ground's own mean there), how far the blend runs.
PADS = [
    (-100, -800, 820, None, 450),       # Castle Town
    (-300, 1250, 780, None, 450),       # Hyrule Castle's rise
    (-400, -5400, 400, -150, 220),      # Lake Hylia's isle
    (-1900, -3450, 380, None, 300),     # Hylia Docks
    (-3700, -3300, 800, None, 450),     # Ordon Village
    (-3900, -1200, 760, None, 450),     # Lon Lon Ranch
    (3000, -4300, 850, None, 500),      # Kokiri Forest
    (3800, -2700, 520, None, 400),      # Lost Woods
    (3900, 300, 850, None, 450),        # Kakariko Village
    (4400, -1400, 420, None, 300),      # Kakariko Graveyard
    (-2000, 1500, 520, None, 400),      # Hyrule Field Ruins
    (-4800, 1400, 850, None, 450),      # Gerudo Fortress mesa
    (-5100, -300, 420, None, 350),      # Haunted Wasteland
    (-3300, 2800, 700, None, 450),      # Desert Colossus
    (500, 2900, 700, None, 600),        # Temple of Time mesa
    (-4100, 4500, 780, None, 450),      # Snowpeak Lodge shelf
    (-2300, 5300, 380, None, 450),      # Snowpeak Summit
    (300, 5100, 460, None, 400),        # Lanayru Spring
    (3600, 3100, 820, None, 600),       # Goron City shelf
    (4900, 5000, 380, None, 200),       # Death Mountain's crater floor (the crater cut keeps its rim)
    (4900, 1700, 400, None, 400),       # Death Mountain Trail
    (1200, 1850, 650, None, 300),       # Zora's Domain shore
]

# Dirt roads: chains of (x, z). They are painted on the ground.
ROADS = [
    [(-100, -800), (-200, 100), (-300, 450)],                                   # Castle Town south to the castle gate
    [(-100, -800), (-900, -1500), (-1900, -2600), (-1900, -3300)],              # Castle Town north west to the docks
    [(-1900, -2600), (-2900, -3100), (-3700, -3300)],                           # docks road to Ordon
    [(-900, -600), (-2400, -1100), (-3900, -1200)],                             # Castle Town west to Lon Lon
    [(-3700, -3300), (-3900, -2300), (-3900, -1200)],                           # Ordon south to Lon Lon
    [(-3900, -1200), (-4600, -700), (-5100, -300)],                             # Lon Lon to the wasteland
    [(-3900, -1200), (-4400, 200), (-4900, 1400)],                              # Lon Lon to the Gerudo
    [(-900, -300), (-1600, 700), (-2000, 1500)],                                # Castle Town to the ruins
    [(-2000, 1500), (-2700, 2100), (-3300, 2800)],                              # ruins to the Colossus
    [(-4900, 1400), (-4200, 2100), (-3300, 2800)],                              # Gerudo to the Colossus
    [(-3300, 2800), (-3700, 3700), (-4100, 4500)],                              # Colossus up to Snowpeak Lodge
    [(-4100, 4500), (-3200, 5000), (-2300, 5300)],                              # lodge to the summit
    [(-2300, 5300), (-1000, 5300), (300, 5100)],                                # summit down to the spring
    [(300, 5100), (500, 4000), (500, 2900)],                                    # spring to the temple
    [(-300, 1000), (100, 2000), (500, 2900)],                                   # castle to the temple
    [(500, 2900), (1600, 3000), (2600, 3100), (3600, 3100)],                    # temple to Goron City
    [(3600, 3100), (4300, 3900), (4700, 4700)],                                 # Goron City up to the crater
    [(3600, 3100), (4400, 2400), (4900, 1700)],                                 # Goron City to the trail
    [(4900, 1700), (4500, 900), (3900, 300)],                                   # the trail down to Kakariko
    [(3900, 300), (4300, -500), (4400, -1400)],                                  # Kakariko to the graveyard
    [(3900, 300), (3100, -500), (2500, -900)],                                  # Kakariko to the river
    [(2500, -900), (1200, -900), (-100, -800)],                                 # the river to Castle Town
    [(-300, 1000), (600, 1500), (1200, 1800)],                                  # castle to Zora's Domain
    [(4400, -1400), (4100, -2100), (3800, -2700)],                               # graveyard to the Lost Woods
    [(3800, -2700), (3300, -3500), (3000, -4300)],                              # Lost Woods to Kokiri
    [(1700, -2900), (2600, -2800), (3800, -2700)],                              # Great Hylia Bridge east end to the woods
    [(1700, -2900), (600, -2400), (-100, -1700), (-100, -800)],                 # the bridge west end to Castle Town
    [(-1900, -3500), (-1300, -4300), (-800, -4900)],                            # the docks to Lake Hylia's bridge
]

# Ground cover regions (x, z, radius): the strongest wins where they overlap; the rest is Hyrule Field meadow.
FOREST = [(3000, -4300, 1500), (3900, -2700, 900), (4800, -3800, 900), (2200, -4900, 700)]
FARMS = [(-3300, -2500, 500), (-4400, -2700, 450), (-3200, -800, 450)]
DESERT = [(-5000, 1300, 1800), (-3600, 2700, 1300), (-5600, -500, 1100), (-5900, 2600, 1000)]
REDROCK = [(4400, 4300, 1800), (3600, 3100, 1100), (5300, 2600, 900)]
SNOW = [(-4200, 5000, 2000), (-2300, 5600, 1300), (-6000, 4000, 1000)]
CRATER = [(4900, 5000, 360)]
PAVED = {"Castle Town": 0.75, "Hyrule Castle": 0.55, "Kakariko Village": 0.35, "Gerudo Fortress": 0.5, "Temple of Time": 0.5,
         "Goron City": 0.45, "Desert Colossus": 0.35, "Zora's Domain": 0.3}

# Where the seven hireable allies wait, in shared/ally.h order (the server puts ally kind i at station i): Kokiri, Zora, Goron, Gerudo,
# Jones, Peter, Master Chief.
ALLY_SPOTS = [
    (2700, -3900, 0),      # Kokiri at the Kokiri Forest's gate
    (1000, 1350, 1),       # Zora on the Domain's north shore
    (3300, 2700, 2),       # Goron at Goron City's door
    (-4500, 1000, 3),      # Gerudo at the fortress gate
    (-400, -1500, 4),      # Jones at Castle Town's north gate
    (-3500, -900, 5),      # Peter at Lon Lon Ranch
    (3600, 700, 6),        # Master Chief at Kakariko's south road
]
# Carts (the game's buggies) stand where the roads meet.
CART_SPOTS = [(-1900, -2600), (-900, -300), (-3900, -1900), (2400, -900), (500, 2000), (-2700, 2100), (4500, 2300)]
SPAWN = (-500, -1700)   # the lobby: the meadow north of Castle Town's gate
