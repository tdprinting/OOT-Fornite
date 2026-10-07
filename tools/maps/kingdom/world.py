"""Builds the whole of Hyrule Kingdom (ground + every landmark) as one geom.World. Used by the exporter and by the Blender script."""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
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
