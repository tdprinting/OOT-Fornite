"""Great Hyrule Kingdom: where the places are. Game units (x east, z south), from the candidate sites in
/mnt/project-files/landscape-map/poi_candidates.json (flat, dry ground on the cleaned landscape).

Step 2 of the map: 20 of 24 places are picked so far. The names are placeholders until the places are built;
the four missing places are still to choose. Radius is the place's own footprint, as in moonfall/layout.py."""

MAP_NAME = 'great_hyrule_kingdom'
MAP_TITLE = 'Great Hyrule Kingdom'
MAP_INDEX = 14   # not registered in shared/map.h yet

# name, x, z, radius, the place's landscape candidate number
POIS = [
    ("Candidate 00",  2172, -3066, 450, 0),
    ("Candidate 01", -1767, -5374, 450, 1),
    ("Candidate 02",  3794, -3248, 450, 2),
    ("Candidate 03",   377, -4341, 450, 3),
    ("Candidate 04", -1303, -3673, 450, 4),
    ("Candidate 05",  1593,  2034, 450, 5),
    ("Candidate 06", -5590, -2156, 450, 6),
    ("Candidate 07",  4026, -1913, 450, 7),
    ("Candidate 08",  2868,  -395, 450, 8),
    ("Candidate 09", -3794, -5374, 450, 9),
    ("Candidate 10", -2462, -2702, 450, 10),
    ("Candidate 11", -3621, -4159, 450, 11),
    ("Candidate 12", -4432, -2884, 450, 12),
    ("Candidate 13",  4895,   -91, 450, 13),
    ("Candidate 14",  4547, -4099, 450, 14),
    ("Candidate 15",  5011, -2459, 450, 15),
    ("Candidate 16",  2983, -4038, 450, 16),
    ("Candidate 17",  1709, -5859, 450, 17),
    ("Candidate 18", -3910,   455, 450, 18),
    ("Candidate 19", -4374, -1488, 450, 19),
]
assert len(POIS) == 20
