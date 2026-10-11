"""Builds the whole of Moonfall Isle (ground + every landmark) as one geom.World. Used by the exporter, the checks and the Blender script.
The shared pieces (geom, textures, surface maps, routes) come from tools/maps/kingdom; the terrain, kit and places are this map's own."""
import os, sys
HERE = os.path.dirname(os.path.abspath(__file__))
KINGDOM = os.path.join(os.path.dirname(HERE), 'kingdom')
if HERE not in sys.path: sys.path.insert(0, HERE)
if KINGDOM not in sys.path: sys.path.append(KINGDOM)      # after this map's modules: terrain/layout/kit/pois are ours, geom/textures/routes are shared
import numpy as np
import terrain, kit, geom, textures
from layout import *

def build(only=None):
    heights = terrain.vertex_grid()
    kit.HGRID = heights
    T = textures.build_all()
    geom.TEX_UNITS.clear(); geom.TEX_UNITS.update({k: v[1] for k, v in T.items()})
    w = geom.World()
    import pois
    pois.build_all(w, only)
    return w, heights, T
