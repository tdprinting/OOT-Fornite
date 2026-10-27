"""Pulls one area of Hyrule Before the Third Storm out of its Blender file for the game exporter (export.py).

Run once per area (Blender 5.1, headless; about a minute an area):
    blender -b <map>/areas-compact/poi-NN.blend --factory-startup --python tools/maps/hyrule_third_storm/blender_extract.py -- --out <dir>

<map> is the map project (C:/Users/mexic/Documents/Claude/zelda battle royale ultimate map/map). The area files are already in the compact
frame (metres, south-west origin, +z up; map/compact/README.md). Writes <dir>/poi-NN.npz (triangles) and <dir>/poi-NN.json (markers).

What is kept, by the collection an object lives in:
  ground     terrain, paths, roads                                  drawn, collision
  solid      buildings, walls, stairs, cliffs, landmarks, crossings  drawn, collision
  prop       kit instances (props, foliage, prep, scenic ...)        drawn; the big ones also get a box collider
  water      water surfaces                                          not drawn as mesh: become pools (the game draws its own water)
Skipped: markers, cameras, lights, story text, atmosphere (mist sheets), grass and flower scatter, interiors' clutter under 0.4 m.
Every mesh is simplified here with Blender's Decimate (planar, then collapse) so the game gets a PS2-sized map; export.py checks the totals."""
import bpy, bmesh, sys, os, re, json, math
import numpy as np
from mathutils import Matrix, Vector

argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
def arg(name, default=None):
    return argv[argv.index(name) + 1] if name in argv else default
OUT = arg("--out")
SCALE = float(arg("--detail", "1.0"))     # multiplies every keep ratio below
FAR = "--far" in argv                     # the far-away copy: ground and solid only, cut hard (poi-NN-far.npz)
os.makedirs(OUT, exist_ok=True)
AREA = re.search(r"poi-(\d\d)", bpy.data.filepath).group(1)

# keep ratios (share of triangles kept) and the fewest triangles an object keeps
RATIO = {"ground": 0.20, "solid": 0.18, "prop": 0.15}
MIN_TRIS = {"ground": 200, "solid": 16, "prop": 8}
GROUND = ("terrain", "paths", "roads", "path", "road")
SOLID = ("building", "wall", "stair", "cliff", "landmark", "crossing", "structure", "temple", "statue", "city", "ring", "fence", "storm",
         "scars", "circulation", "rooms", "deku tree", "border", "maze", "graves", " b0", " b1", " f-", " l0", " h0", " p0", " r2", "gates",
         "fillers", "rockfall", "barn", "orchard", "corral", "lantern")
SKIP = ("marker", "camera", "light", "story text", "atmosphere", "proxies", "kit library", "| world", "gameplay")
# whole objects to leave out: scatter systems, mist, decals, sky stand-ins (a mesh called "pads grass" is ground, so no bare "grass" here)
SKIP_NAME = re.compile(r"scatter|tuft|weeds?|mist|clover|petal|haze|fog|cloud|sky|horizon|placeholder|stand-in|collision hull|decal|"
                       r"puddle|glow|light cone|god ?ray|smoke|ember|spark", re.I)
# kits instanced in their thousands that are only ground dressing
KIT_SKIP = re.compile(r"grass|tuft|weed|flower|clover|petal|pebble|litter|fern|reed", re.I)
WATER_NAME = re.compile(r"water|lake|pool|river|stream|spring|fountain|inlet|cascade|falls|brook|oasis|pond|plunge|channel", re.I)


def classify(path):
    p = " " + path.lower()
    if any(k in p for k in SKIP): return None
    if "water" in p: return "water"
    if "interior" in p: return "prop"
    if any(k in p for k in GROUND): return "ground"
    if any(k in p for k in SOLID): return "solid"
    return "prop"


# ---- colours ------------------------------------------------------------------------------------------------------------------------------
_img_mean = {}
def image_mean(img):
    if img.name in _img_mean: return _img_mean[img.name]
    c = None
    try:
        if img.size[0] > 0:
            px = np.array(img.pixels[:], dtype=np.float32).reshape(-1, img.channels)
            c = px[::7, :3].mean(axis=0)
    except Exception:
        c = None
    _img_mean[img.name] = c
    return c

def lin2srgb(c):
    c = np.clip(np.asarray(c, dtype=np.float64), 0.0, 1.0)
    return np.where(c <= 0.0031308, c * 12.92, 1.055 * np.power(c, 1 / 2.4) - 0.055)

_mat_col = {}
def material_colour(mat):
    """sRGB 0..255 for a material: the colours feeding its surface shader's base colour (RGB nodes, ramps, unlinked colour sockets, image means),
    averaged; falls back to the viewport colour."""
    if mat is None: return (150, 150, 150)
    if mat.name in _mat_col: return _mat_col[mat.name]
    cols = []
    if mat.use_nodes and mat.node_tree:
        nt = mat.node_tree
        bsdf = next((n for n in nt.nodes if n.type in ("BSDF_PRINCIPLED", "BSDF_DIFFUSE", "EMISSION")), None)
        start = None
        if bsdf:
            start = bsdf.inputs.get("Base Color") or bsdf.inputs.get("Color")
        if start is not None:
            if not start.is_linked: cols.append(tuple(start.default_value)[:3])
            seen = set(); frontier = [l.from_node for l in start.links]
            for _ in range(8):
                nxt = []
                for n in frontier:
                    if n is None or n.name in seen: continue
                    seen.add(n.name)
                    if n.type == "RGB": cols.append(tuple(n.outputs[0].default_value)[:3])
                    elif n.type == "VALTORGB":
                        els = n.color_ramp.elements
                        cols.extend(tuple(e.color)[:3] for e in els)
                    elif n.type == "TEX_IMAGE" and n.image is not None:
                        m = image_mean(n.image)
                        if m is not None: cols += [tuple(m)] * 3
                    for s in n.inputs:
                        if s.type == "RGBA":
                            if s.is_linked: nxt += [l.from_node for l in s.links]
                            else: cols.append(tuple(s.default_value)[:3])
                        elif s.is_linked and n.type not in ("TEX_IMAGE",):
                            nxt += [l.from_node for l in s.links if l.from_node.type in ("MIX", "MIX_RGB", "VALTORGB", "RGB", "TEX_IMAGE", "GROUP", "HUE_SAT", "BRIGHTCONTRAST", "INVERT", "GAMMA", "CURVE_RGB")]
                frontier = nxt
    if cols:
        c = lin2srgb(np.mean(np.array(cols, dtype=np.float64), axis=0))
    else:
        c = np.array(mat.diffuse_color[:3])
    out = tuple(int(round(v * 255)) for v in np.clip(c, 0, 1))
    _mat_col[mat.name] = out
    return out


# ---- meshes -------------------------------------------------------------------------------------------------------------------------------
MATS = []          # material names, by index
def mat_index(name):
    if name not in MATS: MATS.append(name)
    return MATS.index(name)

def simplified_mesh(src_obj, ratio, keep_min):
    """A triangulated, decimated copy of the object's evaluated mesh in its local space: (positions (V,3), tris (T,3), mat slot per tri,
    corner colours (T,3,3) or None)."""
    dg = bpy.context.evaluated_depsgraph_get()
    ev = src_obj.evaluated_get(dg)
    try:
        me = bpy.data.meshes.new_from_object(ev, preserve_all_data_layers=True, depsgraph=dg)
    except Exception:
        return None
    if me is None or len(me.polygons) == 0:
        if me: bpy.data.meshes.remove(me)
        return None
    for i, m in enumerate(src_obj.material_slots):
        if i < len(me.materials): me.materials[i] = m.material
    n0 = sum(len(p.vertices) - 2 for p in me.polygons)
    tmp = bpy.data.objects.new("__x", me)
    bpy.context.scene.collection.objects.link(tmp)
    r = min(1.0, max(ratio, keep_min / max(1, n0)))
    if n0 > 64:
        d = tmp.modifiers.new("plan", "DECIMATE"); d.decimate_type = "DISSOLVE"; d.angle_limit = math.radians(1.5); d.delimit = {"MATERIAL"}
    if r < 0.999:
        t = tmp.modifiers.new("tri", "TRIANGULATE")
        d = tmp.modifiers.new("dec", "DECIMATE"); d.decimate_type = "COLLAPSE"; d.ratio = r; d.use_collapse_triangulate = True
    dg = bpy.context.evaluated_depsgraph_get()
    m2 = tmp.evaluated_get(dg).to_mesh()
    m2.calc_loop_triangles()
    V = np.empty(len(m2.vertices) * 3, np.float32); m2.vertices.foreach_get("co", V); V = V.reshape(-1, 3)
    nt = len(m2.loop_triangles)
    T = np.empty(nt * 3, np.int32); m2.loop_triangles.foreach_get("vertices", T); T = T.reshape(-1, 3)
    MI = np.empty(nt, np.int32); m2.loop_triangles.foreach_get("material_index", MI)
    CC = None
    ca = m2.color_attributes.active_color if hasattr(m2, "color_attributes") else None
    if ca is not None and len(ca.data) > 0:
        try:
            raw = np.empty(len(ca.data) * 4, np.float32); ca.data.foreach_get("color_srgb" if hasattr(ca.data[0], "color_srgb") else "color", raw)
            raw = raw.reshape(-1, 4)[:, :3]
            L = np.empty(nt * 3, np.int32); m2.loop_triangles.foreach_get("loops", L); L = L.reshape(-1, 3)
            CC = raw[L] if ca.domain == "CORNER" else raw[T] if ca.domain == "POINT" else None
        except Exception:
            CC = None
    slots = [ms.material for ms in tmp.material_slots]
    tmp.to_mesh_clear()
    bpy.data.objects.remove(tmp)
    bpy.data.meshes.remove(me)
    return V, T, MI, CC, slots, n0


TRI_P = []; TRI_C = []; TRI_K = []; TRI_M = []; TRI_O = []
OBJS = []          # object names, by index (for the debug report: which object a bad triangle came from)
KIND = {"ground": 0, "solid": 1, "prop": 2}
def add_mesh(V, T, MI, CC, slots, mw, kind, oname):
    if len(T) == 0: return
    Vw = (np.c_[V, np.ones(len(V))] @ np.array(mw).T)[:, :3].astype(np.float32)
    P = Vw[T]
    cols = np.zeros((len(T), 3), np.uint8)
    mats = np.zeros(len(T), np.uint16)
    lut_c = []; lut_m = []
    for s in slots or [None]:
        lut_c.append(material_colour(s)); lut_m.append(mat_index(s.name if s else "<none>"))
    if not lut_c: lut_c = [(150, 150, 150)]; lut_m = [mat_index("<none>")]
    mi = np.clip(MI, 0, len(lut_c) - 1)
    cols[:] = np.array(lut_c, np.uint8)[mi]
    mats[:] = np.array(lut_m, np.uint16)[mi]
    if CC is not None:   # a painted mesh (terrain blends): its corner colours times the material's
        cc = np.clip(CC.mean(axis=1), 0, 1)
        if cc.std() > 0.01 or cc.mean() < 0.95:
            cols = np.clip(cc * 255.0, 0, 255).astype(np.uint8)
    TRI_P.append(P); TRI_C.append(cols); TRI_K.append(np.full(len(T), KIND[kind], np.uint8)); TRI_M.append(mats)
    if oname not in OBJS: OBJS.append(oname)
    TRI_O.append(np.full(len(T), OBJS.index(oname), np.uint32))


def coll_paths():
    """object -> the path of collections it sits in (first one found), for every object under the area's top collection."""
    out = {}
    def walk(c, path):
        for o in c.objects: out.setdefault(o.name, path)
        for ch in c.children: walk(ch, path + "/" + ch.name)
    for top in bpy.context.scene.collection.children:
        walk(top, top.name)
    return out


def main():
    paths = coll_paths()
    report = {"area": AREA, "objects": 0, "kits": 0, "instances": 0, "source_tris": 0, "kept_tris": 0, "skipped": []}
    markers = {"loot": [], "floor": [], "drops": [], "exits": [], "water": [], "colliders": [], "buildings": [], "climb": []}
    kit_cache = {}
    # 1. plain meshes (and curves, text is skipped)
    for o in list(bpy.data.objects):
        if o.name not in paths: continue
        kind = classify(paths[o.name])
        if kind is None: continue
        if o.type == "EMPTY": continue
        if o.type not in ("MESH", "CURVE", "SURFACE", "META"): continue
        if o.hide_render: continue
        if SKIP_NAME.search(o.name) and not (kind == "water" and WATER_NAME.search(o.name)):
            report["skipped"].append(o.name); continue
        if kind == "water" or (WATER_NAME.search(o.name) and ("surface" in o.name.lower() or "water" in o.name.lower()) and "wheel" not in o.name.lower()):
            # a water surface: its outline and level become a pool
            dg = bpy.context.evaluated_depsgraph_get()
            me = o.evaluated_get(dg).to_mesh()
            if me and len(me.vertices):
                V = np.array([o.matrix_world @ v.co for v in me.vertices], np.float32)
                lvl = re.search(r"surface\s*([+-]?\d+(?:\.\d+)?)", o.name)
                markers["water"].append({"name": o.name, "verts": V[:, :3].round(2).tolist() if len(V) <= 4000 else V[::max(1, len(V) // 4000), :3].round(2).tolist(),
                                         "zmin": float(V[:, 2].min()), "zmax": float(V[:, 2].max()),
                                         "level_name": float(lvl.group(1)) if lvl else None})
            o.evaluated_get(dg).to_mesh_clear()
            continue
        if FAR and kind == "prop": continue
        got = simplified_mesh(o, RATIO[kind] * SCALE, MIN_TRIS[kind] if not FAR else 4)
        if not got: continue
        V, T, MI, CC, slots, n0 = got
        report["objects"] += 1; report["source_tris"] += n0; report["kept_tris"] += len(T)
        add_mesh(V, T, MI, CC, slots, o.matrix_world, kind, o.name)
        mb = re.search(r"([A-Z0-9]{2,3}-B\d\d)", o.name) if kind == "solid" else None
        if mb:
            b = mb.group(1)
            Vw = (np.c_[V, np.ones(len(V))] @ np.array(o.matrix_world).T)[:, :3]
            markers["buildings"].append({"id": b, "name": o.name, "min": Vw.min(0).round(2).tolist(), "max": Vw.max(0).round(2).tolist()})
        if re.search(r"ivy|vine|ladder|climb", o.name, re.I):
            Vw = (np.c_[V, np.ones(len(V))] @ np.array(o.matrix_world).T)[:, :3]
            markers["climb"].append({"name": o.name, "min": Vw.min(0).round(2).tolist(), "max": Vw.max(0).round(2).tolist()})
    # 2. collection instances (kits): every instance the depsgraph makes, except scatter of grass and flowers
    dg = bpy.context.evaluated_depsgraph_get()
    uses = {}; seen = {}
    for inst in dg.object_instances:   # how often each kit is used: a kit used 200 times keeps far fewer triangles than a one-off
        if inst.is_instance: uses[inst.object.original.name] = uses.get(inst.object.original.name, 0) + 1
    for inst in (dg.object_instances if not FAR else ()):
        if not inst.is_instance: continue
        src = inst.object.original
        par = inst.parent.original if inst.parent else None
        if par is None or par.name not in paths: continue
        kind = classify(paths[par.name])
        if kind is None or kind == "water": continue
        if src.type not in ("MESH",): continue
        if SKIP_NAME.search(src.name) or SKIP_NAME.search(par.name) or KIT_SKIP.search(src.name): continue
        dims = np.array(src.dimensions) * np.array(inst.matrix_world.to_scale())
        if max(dims) < 0.4: continue                 # small clutter: not worth its triangles
        is_scatter = par.type == "MESH"              # instanced by a geometry-nodes scatter on a mesh (trees, rocks, bushes)
        if is_scatter and max(dims) < 3.0: continue  # scattered bushes and stones: cover comes from the trees and big rocks
        key = src.name
        seen[key] = seen.get(key, 0) + 1
        cap = 120 if is_scatter else 160
        if uses[key] > cap and seen[key] % math.ceil(uses[key] / cap): continue   # at most about 120-160 of one kit an area (forests: every k-th tree)
        if key not in kit_cache:
            rep = min(1.0, (8.0 / max(1, uses[key])) ** 0.5)   # repeated kits are cut harder
            kit_cache[key] = simplified_mesh(src, RATIO["prop"] * SCALE * (0.6 if is_scatter else 1.0) * max(0.25, rep), MIN_TRIS["prop"])
            report["kits"] += 1
        got = kit_cache[key]
        if not got: continue
        V, T, MI, CC, slots, n0 = got
        mw = inst.matrix_world.copy()
        report["instances"] += 1; report["kept_tris"] += len(T)
        name = par.name if not is_scatter else par.name + " scatter " + src.name
        add_mesh(V, T, MI, CC, slots, mw, kind if kind != "ground" else "prop", name)
        Vw = (np.c_[V, np.ones(len(V))] @ np.array(mw).T)[:, :3]
        lo, hi = Vw.min(0), Vw.max(0)
        size = hi - lo
        if size[2] > 0.9 and max(size[0], size[1]) > 0.5:   # tall enough to stop you: a box (trees: the trunk only)
            tree = bool(re.search(r"FOL-0[1-7]|tree|oak|pine|trunk|deku", src.name + " " + par.name, re.I))
            if tree:
                c = (lo + hi) / 2; r = max(0.35, min(size[0], size[1]) * 0.09)
                lo2 = [c[0] - r, c[1] - r, lo[2]]; hi2 = [c[0] + r, c[1] + r, lo[2] + min(size[2], 4.0)]
                markers["colliders"].append({"name": src.name, "min": [round(v, 2) for v in lo2], "max": [round(v, 2) for v in hi2], "tree": True})
            else:
                markers["colliders"].append({"name": src.name, "min": lo.round(2).tolist(), "max": hi.round(2).tolist(), "tree": False})
    # 3. markers: chests, floor loot, drop spots, exits
    for o in bpy.data.objects:
        if o.name not in paths: continue
        p = paths[o.name].lower(); n = o.name
        loc = [round(v, 2) for v in o.matrix_world.translation]
        if re.search(r"-C\d\d", n) and "socket" not in n.lower() and ("loot" in p or "gameplay" in p) and not re.search(r"-C\d\d\.\d", n):
            markers["loot"].append({"name": n, "loc": loc})
        elif re.search(r"-Fl-\d\d", n):
            markers["floor"].append({"name": n, "loc": loc})
        elif re.search(r"-DS\d", n) or re.search(r"\bDS\d", n):
            markers["drops"].append({"name": n, "loc": loc})
        elif ("exit" in p or "markers" in p) and re.match(r"(E\d|HF-J|[A-Z]{2}-JOIN)", n):
            markers["exits"].append({"name": n, "loc": loc})
    # sockets without a chest instance (KF/LW keep chest and socket apart): use the socket when its chest is missing
    have = {re.search(r"([A-Z]{2}-C\d\d)", m["name"]).group(1) for m in markers["loot"] if re.search(r"([A-Z]{2}-C\d\d)", m["name"])}
    for o in bpy.data.objects:
        if o.name in paths and "socket" in o.name.lower():
            m = re.search(r"([A-Z]{2}-C\d\d)", o.name)
            if m and m.group(1) not in have:
                markers["loot"].append({"name": o.name, "loc": [round(v, 2) for v in o.matrix_world.translation]}); have.add(m.group(1))
    P = np.concatenate(TRI_P) if TRI_P else np.zeros((0, 3, 3), np.float32)
    if FAR:
        np.savez_compressed(os.path.join(OUT, f"poi-{AREA}-far.npz"), pos=P, col=np.concatenate(TRI_C), kind=np.concatenate(TRI_K),
                            mat=np.concatenate(TRI_M), obj=np.concatenate(TRI_O))
        print(f"AREA {AREA} far: {len(P)} triangles"); return
    np.savez_compressed(os.path.join(OUT, f"poi-{AREA}.npz"), pos=P, col=np.concatenate(TRI_C), kind=np.concatenate(TRI_K),
                        mat=np.concatenate(TRI_M), obj=np.concatenate(TRI_O))
    report["mats"] = MATS
    report["mat_colours"] = {k: list(v) for k, v in _mat_col.items()}
    report["objects_list"] = OBJS
    report["markers"] = markers
    report["tris"] = int(len(P))
    with open(os.path.join(OUT, f"poi-{AREA}.json"), "w") as f: json.dump(report, f)
    print(f"AREA {AREA}: {report['objects']} objects, {report['kits']} kits, {report['instances']} instances, {len(P)} triangles kept "
          f"(source {report['source_tris']}); loot {len(markers['loot'])} floor {len(markers['floor'])} drops {len(markers['drops'])} water {len(markers['water'])} colliders {len(markers['colliders'])}")

main()
