"""2D concept sheet of the Moonfall Bus: four views (side, front, rear, top) drawn as cel-shaded SVG, rendered to PNG with Chromium.
Usage: python concept_sheet.py <out.png>"""
import math, sys, os, subprocess

PW, PH = 900, 760          # one panel
OUT = sys.argv[-1]
C = dict(lac="#7A3119", lac2="#5A2010", lac3="#9A4426", gold="#E6C04A", gold2="#B4882A", brass="#C49A4A", brass2="#8A6428", rune="#4FE0F5",
         glass="#3A86A8", glass2="#7CC8E0", moon="#9A9A90", moon2="#6E6D64", moon3="#4A4842", eye="#FFD63A", eye2="#FFF2B0", sock="#C8401E",
         pupil="#1A1206", teeth="#EFE8D0", mouth="#1C1410", flame="#FF8A1A", flame2="#FFE07A", leaf="#55A33E", leaf2="#B6DC6E",
         wood="#9A6433", wood2="#6A4320", iron="#5E6168", line="#2A1408", lantern="#FFD27A", red="#B23A2A", blue="#2E4A86",
         goron="#C4915A")

def g(*items, **attrs):
    a = " ".join(f'{k.replace("_", "-")}="{v}"' for k, v in attrs.items())
    return f"<g {a}>" + "".join(items) + "</g>"
def rect(x, y, w, h, fill, rx=0, stroke=True, sw=3, extra=""):
    s = f' stroke="{C["line"]}" stroke-width="{sw}"' if stroke else ""
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{fill}"{s} {extra}/>'
def circ(cx, cy, r, fill, stroke=True, sw=3, extra=""):
    s = f' stroke="{C["line"]}" stroke-width="{sw}"' if stroke else ""
    return f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{fill}"{s} {extra}/>'
def ell(cx, cy, rx, ry, fill, stroke=True, sw=3, extra=""):
    s = f' stroke="{C["line"]}" stroke-width="{sw}"' if stroke else ""
    return f'<ellipse cx="{cx}" cy="{cy}" rx="{rx}" ry="{ry}" fill="{fill}"{s} {extra}/>'
def path(d, fill="none", stroke=None, sw=3, extra=""):
    st = stroke or C["line"]
    return f'<path d="{d}" fill="{fill}" stroke="{st}" stroke-width="{sw}" stroke-linejoin="round" stroke-linecap="round" {extra}/>'
def line(x1, y1, x2, y2, stroke=None, sw=3, extra=""):
    return f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{stroke or C["line"]}" stroke-width="{sw}" stroke-linecap="round" {extra}/>'
def text(x, y, s, size=18, fill="#2A1408", anchor="start", weight="normal", italic=False):
    st = ' font-style="italic"' if italic else ""
    return f'<text x="{x}" y="{y}" font-size="{size}" fill="{fill}" text-anchor="{anchor}" font-weight="{weight}"{st}>{s}</text>'
def triforce(cx, cy, w, fill=None, sw=1.5):
    """three gold triangles, point up, width w, centred at (cx, cy)"""
    h = w * 0.866; f = fill or C["gold"]; out = ""
    for (ox, oy) in ((0, -h/4), (-w/4, h/4), (w/4, h/4)):
        x, y = cx + ox, cy + oy
        out += path(f"M{x} {y - h/4} L{x + w/4} {y + h/4} L{x - w/4} {y + h/4} Z", f, C["line"], sw)
    return out
def callout(x, y, tx, ty, s, anchor="start"):
    return line(x, y, tx, ty, "#3A2A1A", 1.6) + circ(x, y, 3.5, "#3A2A1A", False) + text(tx + (6 if anchor == "start" else -6), ty + 5, s, 16, "#3A2A1A", anchor)

DEFS = f'''
<defs>
  <linearGradient id="sky" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#5E8FD6"/><stop offset="0.55" stop-color="#A9CDEB"/><stop offset="1" stop-color="#F3D7B0"/></linearGradient>
  <radialGradient id="moonG" cx="0.38" cy="0.32" r="0.75"><stop offset="0" stop-color="#B8B8AE"/><stop offset="0.6" stop-color="{C['moon']}"/><stop offset="1" stop-color="{C['moon2']}"/></radialGradient>
  <radialGradient id="eyeG" cx="0.5" cy="0.5" r="0.5"><stop offset="0" stop-color="#FFF0A0"/><stop offset="0.45" stop-color="#FFC21A"/><stop offset="0.8" stop-color="#F07A14"/><stop offset="1" stop-color="#B8301A"/></radialGradient>
  <linearGradient id="lacG" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{C['lac3']}"/><stop offset="0.5" stop-color="{C['lac']}"/><stop offset="1" stop-color="{C['lac2']}"/></linearGradient>
  <linearGradient id="glassG" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="{C['glass2']}"/><stop offset="1" stop-color="{C['glass']}"/></linearGradient>
  <linearGradient id="flameG" x1="0" y1="1" x2="0" y2="0"><stop offset="0" stop-color="{C['flame']}"/><stop offset="1" stop-color="{C['flame2']}"/></linearGradient>
  <linearGradient id="brassG" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#E2BC6A"/><stop offset="1" stop-color="{C['brass2']}"/></linearGradient>
  <filter id="glow" x="-50%" y="-50%" width="200%" height="200%"><feGaussianBlur stdDeviation="4" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
  <filter id="glow2" x="-100%" y="-100%" width="300%" height="300%"><feGaussianBlur stdDeviation="9" result="b"/><feMerge><feMergeNode in="b"/><feMergeNode in="SourceGraphic"/></feMerge></filter>
</defs>'''

# ------------------------------------------------------------------------------------------------------------------- the Moon
def craters(cx, cy, R, seed=3, n=14, skip=None):
    import random
    rnd = random.Random(seed); out = ""
    for _ in range(n):
        a = rnd.uniform(0, 2 * math.pi); d = rnd.uniform(0.15, 0.85) * R
        x, y = cx + math.cos(a) * d, cy + math.sin(a) * d
        if skip and skip(x, y): continue
        r = rnd.uniform(0.05, 0.13) * R
        out += ell(x, y, r, r * 0.8, C["moon2"], True, 2) + ell(x - r * 0.15, y - r * 0.2, r * 0.7, r * 0.5, "#8A8A80", False)
    return out
def net(cx, cy, R, collar_y):
    out = ""
    for k in range(-4, 5):
        x0 = cx + k * R * 0.42
        if abs(k) > 3: continue
        out += path(f"M{x0} {cy + math.sqrt(max(0, R*R - (x0-cx)**2)) * 0.1} Q{cx + k * R * 0.26} {cy + R * 0.75} {cx + k * R * 0.12} {collar_y}", "none", C["gold2"], 2.5)
    for j in (0.35, 0.62, 0.85):
        y = cy + R * j; w = math.sqrt(max(0, 1 - j * j)) * R
        out += path(f"M{cx - w} {y} Q{cx} {y + R * 0.12} {cx + w} {y}", "none", C["gold2"], 2.5)
    return out
def moon_front(cx, cy, R):
    s = circ(cx, cy, R, "url(#moonG)", True, 4)
    s += craters(cx, cy, R, 5, 16, lambda x, y: abs(x - cx) < R * 0.7 and y > cy - R * 0.6 and y < cy + R * 0.7)
    # cracks radiating from the mouth corners and the brow
    for d in ("M-0.62 0.30 L-0.84 0.52", "M0.62 0.30 L0.86 0.50", "M-0.5 -0.5 L-0.72 -0.74", "M0.48 -0.52 L0.66 -0.78", "M0.0 0.78 L0.08 0.95"):
        pts = d.replace("M", "").replace("L", " ").split(); x0, y0, x1, y1 = [float(v) for v in pts]
        s += line(cx + x0*R, cy + y0*R, cx + x1*R, cy + y1*R, C["moon3"], 3)
    # eyes: deep dark sockets, sagging bags, bloodshot orange eyes with tiny pupils that look in at the bridge
    for sx in (-1, 1):
        ex, ey = cx + sx * R * 0.33, cy - R * 0.2
        s += ell(ex, ey + R*0.04, R * 0.25, R * 0.23, C["moon3"], False)                                 # the socket's shadow
        s += ell(ex, ey + R*0.19, R * 0.2, R * 0.06, C["moon2"], True, 2.5)                             # the bag under the eye
        s += ell(ex, ey, R * 0.215, R * 0.2, C["sock"], True, 3)
        s += ell(ex, ey, R * 0.18, R * 0.17, "url(#eyeG)", False, extra='filter="url(#glow2)"')
        for k in range(5):   # veins
            a_ = -0.6 + k * 0.5 + sx * 0.2
            s += line(ex + math.cos(a_ + sx*1.57) * R*0.17, ey + math.sin(a_ + sx*1.57) * R*0.16, ex + math.cos(a_ + sx*1.57) * R*0.09, ey + math.sin(a_ + sx*1.57) * R*0.085, "#C8401E", 1.6)
        s += circ(ex + sx * R * 0.06, ey + R * 0.02, R * 0.045, C["pupil"], False)
        s += circ(ex + sx * R * 0.08, ey - R * 0.02, R * 0.012, "#FFF6DC", False)
    # brows: thick, heavy, slanted hard inward
    for sx in (-1, 1):
        s += path(f"M{cx + sx*R*0.08} {cy - R*0.3} Q{cx + sx*R*0.3} {cy - R*0.56} {cx + sx*R*0.62} {cy - R*0.4} Q{cx + sx*R*0.34} {cy - R*0.42} {cx + sx*R*0.12} {cy - R*0.24} Z", C["moon3"], C["line"], 2.5)
    # the nose is only two flared nostrils and a crease between the eyes
    s += path(f"M{cx - R*0.04} {cy - R*0.1} L{cx} {cy + R*0.08} L{cx + R*0.04} {cy - R*0.1}", "none", C["moon3"], 3)
    for sx in (-1, 1): s += ell(cx + sx * R * 0.1, cy + R * 0.13, R * 0.065, R * 0.04, C["moon3"], False)
    # the grin: stretched wider than the eyes, lips pulled back, two rows of big uneven teeth with a black gap
    my = cy + R * 0.4
    s += path(f"M{cx - R*0.64} {my - R*0.1} Q{cx} {my - R*0.02} {cx + R*0.64} {my - R*0.1} Q{cx + R*0.5} {my + R*0.3} {cx} {my + R*0.34} Q{cx - R*0.5} {my + R*0.3} {cx - R*0.64} {my - R*0.1} Z", C["mouth"], C["moon3"], 4)
    n = 10
    for i in range(n):
        t = (i + 0.5) / n; x = cx - R * 0.58 + t * R * 1.16; bow = (1 - (2*t-1)**2)
        lip_top = my - R*0.1 + bow * R * 0.08; lip_bot = my - R*0.1 + bow * R * 0.44
        h_top = R * 0.12 * (0.8 + 0.2 * bow); h_bot = R * 0.09 * (0.8 + 0.2 * bow)
        s += rect(x - R*0.052, lip_top, R * 0.104, h_top, C["teeth"], 3, True, 2)
        s += rect(x - R*0.047, lip_bot - h_bot, R * 0.094, h_bot, C["teeth"], 3, True, 2)
    s += path(f"M{cx - R*0.66} {my - R*0.12} Q{cx} {my - R*0.03} {cx + R*0.66} {my - R*0.12}", "none", C["moon3"], 5)      # upper lip line
    for sx in (-1, 1): s += path(f"M{cx + sx*R*0.14} {cy + R*0.16} Q{cx + sx*R*0.48} {cy + R*0.2} {cx + sx*R*0.68} {cy + R*0.3}", "none", C["moon3"], 3)   # the creases down from the nostrils
    return s
def moon_side(cx, cy, R, facing=-1):
    """profile: the face is on the `facing` side (-1 = left)"""
    s = circ(cx, cy, R, "url(#moonG)", True, 4)
    s += craters(cx, cy, R, 7, 15, lambda x, y: (x - cx) * facing > R * 0.3)
    f = facing
    s += path(f"M{cx + f*R*0.5} {cy - R*0.6} L{cx + f*R*0.62} {cy - R*0.82}", "none", C["moon3"], 3)
    # brow ridge over a sunken socket, the eye bulging out past the rock
    s += path(f"M{cx + f*R*0.5} {cy - R*0.5} Q{cx + f*R*0.8} {cy - R*0.56} {cx + f*R*0.98} {cy - R*0.34} Q{cx + f*R*0.8} {cy - R*0.42} {cx + f*R*0.54} {cy - R*0.4} Z", C["moon3"], C["line"], 2.5)
    ex, ey = cx + f * R * 0.8, cy - R * 0.18
    s += ell(ex, ey + R*0.03, R * 0.16, R * 0.2, C["moon3"], False) + ell(ex, ey + R*0.19, R*0.13, R*0.05, C["moon2"], True, 2.5)
    s += ell(ex, ey, R * 0.14, R * 0.18, C["sock"], True, 3) + ell(ex, ey, R * 0.11, R * 0.15, "url(#eyeG)", False, extra='filter="url(#glow2)"')
    s += circ(ex + f * R * 0.05, ey + R * 0.02, R * 0.04, C["pupil"], False)
    s += ell(cx + f*R*0.96, cy + R*0.13, R*0.06, R*0.035, C["moon3"], False)   # nostril
    # the grin wraps round the lower front: a dark wedge with big teeth along the lip line
    s += path(f"M{cx + f*R*0.99} {cy + R*0.26} Q{cx + f*R*0.86} {cy + R*0.6} {cx + f*R*0.3} {cy + R*0.9} Q{cx + f*R*0.62} {cy + R*0.72} {cx + f*R*0.9} {cy + R*0.42} Z", C["mouth"], C["moon3"], 4)
    p0 = (cx + f*R*0.99, cy + R*0.26); p1 = (cx + f*R*0.86, cy + R*0.6); p2 = (cx + f*R*0.3, cy + R*0.9)
    for i in range(6):
        t = 0.12 + i * 0.15
        x = (1-t)**2*p0[0] + 2*(1-t)*t*p1[0] + t*t*p2[0]; y = (1-t)**2*p0[1] + 2*(1-t)*t*p1[1] + t*t*p2[1]
        s += rect(x - R*0.035 - f*R*0.06, y - R*0.01, R*0.07, R*0.09, C["teeth"], 3, True, 2)
    s += path(f"M{cx + f*R*0.9} {cy + R*0.2} Q{cx + f*R*0.7} {cy + R*0.34} {cx + f*R*0.62} {cy + R*0.56}", "none", C["moon3"], 3)   # the cheek crease
    return s
def moon_back(cx, cy, R):
    return circ(cx, cy, R, "url(#moonG)", True, 4) + craters(cx, cy, R, 9, 22)

# ------------------------------------------------------------------------------------------------------------------- the burner cage
def cage(cx, top, bottom, w=90):
    s = ""
    ring_y = top
    s += ell(cx, ring_y, w * 0.75, 10, "url(#brassG)", True, 3)
    for sx in (-1, 1): s += rect(cx + sx * w * 0.5 - 4, ring_y, 8, bottom - ring_y, "url(#brassG)", 0, True, 2.5)
    s += rect(cx - w * 0.62, bottom - 6, w * 1.24, 8, "url(#brassG)", 2, True, 2.5)
    # firebox: round iron stove with rock bands and an ember mouth
    s += rect(cx - 26, ring_y + 26, 52, bottom - ring_y - 32, C["iron"], 8, True, 3)
    for k in range(3): s += rect(cx - 28, ring_y + 34 + k * 14, 56, 6, C["moon2"], 2, True, 1.5)
    s += rect(cx - 12, bottom - 22, 24, 10, C["flame"], 2, True, 2, 'filter="url(#glow)"')
    # flame up into the Moon's collar
    s += path(f"M{cx - 16} {ring_y + 26} Q{cx - 20} {ring_y - 30} {cx} {ring_y - 70} Q{cx + 20} {ring_y - 30} {cx + 16} {ring_y + 26} Z", "url(#flameG)", C["flame"], 2, 'filter="url(#glow2)"')
    s += path(f"M{cx - 7} {ring_y + 20} Q{cx - 8} {ring_y - 20} {cx} {ring_y - 44} Q{cx + 8} {ring_y - 20} {cx + 7} {ring_y + 20} Z", C["flame2"], "none", 0)
    return s
def ropes(ring_x, ring_y, w, moon_cx, moon_cy, R, collar_y):
    s = ""
    yj = moon_cy + R * 0.85; wj = math.sqrt(1 - 0.85 * 0.85) * R          # the net's bottom ring: the ropes tie here
    for sx in (-1, 1):
        for k, nx in ((0.5, 0.42), (0.78, 0.92)):
            x0, y0 = ring_x + sx * w * k, ring_y; x1, y1 = moon_cx + sx * wj * nx, yj + (1 - nx) * R * 0.06
            s += line(x0, y0, x1, y1, C["gold2"], 3.5)
            s += circ(x1, y1, 4, C["wood2"], True, 1.5)                     # the knot
            s += line(x0, y0, x0, y0 + 14, C["gold2"], 2) + circ(x0, y0 + 22, 8, C["wood"], True, 2)   # sandbag hanging off the ring
    s += rect(moon_cx - 36, collar_y - 14, 72, 22, C["wood2"], 4, True, 3)   # the mouth collar
    for sx in (-1, 1): s += line(moon_cx + sx * 30, collar_y - 14, moon_cx + sx * wj * 0.42, yj, C["gold2"], 2.5)
    return s

# ------------------------------------------------------------------------------------------------------------------- the coach: side view
def wheel(cx, cy, r):
    s = circ(cx, cy, r, C["wood2"], True, 3) + circ(cx, cy, r - 6, C["wood"], False)
    for k in range(6):
        a = k * math.pi / 3
        s += line(cx, cy, cx + math.cos(a) * (r - 4), cy + math.sin(a) * (r - 4), C["wood2"], 4)
    s += circ(cx, cy, r, "none", True, 3) + circ(cx, cy, 9, C["rune"], True, 2, 'filter="url(#glow)"') + circ(cx, cy, 3, C["blue"], False)
    return s
def leaf(cx, cy, L, W, angle, color=None):
    return f'<g transform="rotate({angle} {cx} {cy})">' + path(f"M{cx} {cy} Q{cx - W} {cy - L*0.45} {cx} {cy - L} Q{cx + W} {cy - L*0.45} {cx} {cy} Z", color or C["leaf"], C["line"], 2.5) + line(cx, cy, cx, cy - L * 0.92, C["leaf2"], 2) + "</g>"
def lantern(cx, cy):
    return rect(cx - 6, cy - 16, 12, 5, C["brass2"], 1, True, 1.5) + line(cx, cy - 12, cx, cy - 4, C["brass2"], 2) + ell(cx, cy + 8, 9, 12, C["lantern"], True, 2, 'filter="url(#glow)"') + rect(cx - 5, cy - 5, 10, 4, C["brass2"], 1, False)
def horse_head(x, y, f=-1, scale=1.0):
    """bronze horse head, nose pointing in direction f, neck base at (x, y)"""
    s = f'<g transform="translate({x} {y}) scale({f*scale} {scale})">'
    s += path("M0 0 Q-8 -28 -30 -40 Q-62 -52 -78 -42 Q-92 -34 -88 -20 Q-86 -10 -70 -8 L-52 -4 Q-30 -2 -18 6 Z", "url(#brassG)", C["line"], 3)
    s += path("M-40 -42 L-48 -62 L-30 -48 Z", "url(#brassG)", C["line"], 2.5) + path("M-28 -40 L-32 -58 L-18 -44 Z", "url(#brassG)", C["line"], 2.5)
    for k in range(4): s += path(f"M{-6 - k*9} {-30 + k*5} L{-14 - k*9} {-46 + k*6} L{-18 - k*9} {-28 + k*5} Z", C["gold"], C["line"], 2)   # mane fins
    s += circ(-62, -36, 5, C["rune"], True, 2, 'filter="url(#glow)"') + ell(-84, -22, 4, 3, C["line"], False)
    s += "</g>"
    return s
def side_view(ox, oy, mirror=False):
    """side view, front to the left; body 480 x 130 at (ox+180 .. ox+660, oy+430 .. oy+560)"""
    bx, by, bw, bh = ox + 190, oy + 445, 470, 128
    s = ""
    # moon, ropes and cage
    mcx, mcy, R = bx + bw * 0.5, oy + 190, 136
    s += moon_side(mcx, mcy, R, -1) + net(mcx, mcy, R, mcy + R - 4)
    s += ropes(mcx, by - 118, 90, mcx, mcy, R, mcy + R + 2)
    s += cage(mcx, by - 118, by - 10)
    # banner pole at the rear
    s += line(bx + bw - 12, by - 70, bx + bw - 12, by + 10, C["wood2"], 4)
    s += path(f"M{bx + bw - 12} {by - 70} L{bx + bw + 70} {by - 58} L{bx + bw + 40} {by - 44} L{bx + bw + 70} {by - 30} L{bx + bw - 12} {by - 36} Z", C["red"], C["line"], 2.5)
    s += triforce(bx + bw + 20, by - 52, 22, sw=1)
    # roof then body
    s += rect(bx + 12, by - 14, bw - 24, 22, C["lac2"], 8, True, 3)
    s += rect(bx, by, bw, bh, "url(#lacG)", 22, True, 4)
    s += rect(bx + 6, by + bh - 34, bw - 12, 12, C["lac2"], 6, False)      # skirt shadow
    # gold bands and the rune line
    s += rect(bx + 10, by + 74, bw - 20, 7, C["gold"], 3, True, 1.5)
    s += rect(bx + 14, by + 86, bw - 28, 3, C["rune"], 1.5, False, 0, 'filter="url(#glow)"')
    s += rect(bx + 10, by + bh - 16, bw - 20, 5, C["gold"], 2, True, 1.5)
    for k in range(6): s += path(f"M{bx + 50 + k*76} {by + 100} l6 -6 l6 6 l-6 6 Z", C["rune"], C["line"], 1.5, 'filter="url(#glow)"')
    # windows: seven arches
    for i in range(7):
        wx = bx + 40 + i * 60; wy = by + 14
        s += path(f"M{wx} {wy + 52} L{wx} {wy + 20} A20 20 0 0 1 {wx + 40} {wy + 20} L{wx + 40} {wy + 52} Z", "url(#glassG)", C["brass2"], 4)
        s += path(f"M{wx + 6} {wy + 48} L{wx + 6} {wy + 22} A14 14 0 0 1 {wx + 20} {wy + 8}", "none", "#BFE6F2", 2.5)
    # crest plaque
    px = bx + bw * 0.5 - 34; py = by + 94
    s += rect(px, py, 68, 30, C["red"], 4, True, 2) + triforce(bx + bw * 0.5, py + 16, 30)
    # hood, grille, lamp; horse head
    s += rect(bx - 62, by + 62, 70, 58, "url(#lacG)", 12, True, 3.5)
    for k in range(4): s += line(bx - 52, by + 76 + k * 10, bx - 20, by + 76 + k * 10, C["brass2"], 2.5)
    s += circ(bx - 56, by + 72, 7, C["rune"], True, 2, 'filter="url(#glow)"')
    s += horse_head(bx - 30, by + 62, -1, 0.9)
    # pilot's bay: glass front panel
    s += rect(bx + 4, by + 12, 30, 56, "url(#glassG)", 6, True, 3)
    s += circ(bx + 19, by + 44, 9, C["goron"], True, 2) + ell(bx + 19, by + 58, 12, 8, C["goron"], True, 2)
    # lanterns
    for i in range(4): s += lantern(bx + 70 + i * 120, by - 4)
    # wheels on iron arms
    for wx in (bx + 70, bx + bw - 70):
        s += rect(wx - 12, by + bh - 20, 24, 26, C["iron"], 3, True, 2.5)
        s += wheel(wx, by + bh + 14, 34)
    # rear deck, rail, steps
    s += rect(bx + bw - 4, by + 92, 60, 12, C["wood"], 3, True, 2.5)
    s += line(bx + bw + 52, by + 92, bx + bw + 52, by + 50, C["brass2"], 3) + line(bx + bw - 2, by + 52, bx + bw + 52, by + 52, C["brass2"], 3)
    for k in range(3): s += rect(bx + bw + 40 + k * 14, by + 104 + k * 12, 24, 6, C["wood"], 2, True, 2)
    # propeller at the rear
    hx, hy = bx + bw + 78, by + 50
    s += rect(bx + bw - 2, hy - 6, hx - bx - bw + 2, 12, "url(#brassG)", 4, True, 2.5)      # the brass arm from the body to the hub
    s += line(bx + bw + 52, by + 52, hx - 10, hy + 2, C["brass2"], 3)
    s += leaf(hx, hy, 70, 18, 0) + leaf(hx, hy, 70, 18, 180) + ell(hx, hy, 10, 26, C["leaf"], True, 2.5)
    s += circ(hx, hy, 9, "url(#brassG)", True, 2.5)
    return s

# ------------------------------------------------------------------------------------------------------------------- front and rear views
def end_view(ox, oy, front=True):
    bx, by, bw, bh = ox + 450 - 70, oy + 445, 140, 128
    mcx, mcy, R = ox + 450, oy + 190, 136
    s = (moon_front if front else moon_back)(mcx, mcy, R) + net(mcx, mcy, R, mcy + R - 4)
    s += ropes(mcx, by - 118, 54, mcx, mcy, R, mcy + R + 2)
    s += cage(mcx, by - 118, by - 10, 60)
    if not front:   # propeller seen face on behind the body, banner, deck rail and steps
        hx, hy = mcx, by + 50
        for a in (0, 90, 180, 270): s += leaf(hx, hy, 104, 24, a + 20)
        s += circ(hx, hy, 10, "url(#brassG)", True, 2.5)
    s += rect(bx + 10, by - 14, bw - 20, 22, C["lac2"], 8, True, 3)
    s += rect(bx, by, bw, bh, "url(#lacG)", 22, True, 4)
    s += rect(bx + 10, by + 74, bw - 20, 7, C["gold"], 3, True, 1.5) + rect(bx + 10, by + bh - 16, bw - 20, 5, C["gold"], 2, True, 1.5)
    if front:
        # glass front with the Goron pilot, hood, grille, lamps, horse head seen head on
        s += rect(bx + 16, by + 12, bw - 32, 56, "url(#glassG)", 8, True, 3)
        s += circ(mcx + 26, by + 44, 10, C["goron"], True, 2) + ell(mcx + 26, by + 60, 14, 8, C["goron"], True, 2)
        s += rect(bx + 14, by + 86, bw - 28, 42, "url(#lacG)", 10, True, 3)
        for k in range(5): line(0, 0, 0, 0)
        for k in range(4): s += line(bx + 36, by + 98 + k * 8, bx + bw - 36, by + 98 + k * 8, C["brass2"], 2.5)
        for sx in (-1, 1): s += circ(mcx + sx * 50, by + 96, 8, C["rune"], True, 2, 'filter="url(#glow)"')
        s += rect(mcx - 22, by + 100, 44, 24, C["red"], 3, True, 2) + triforce(mcx, by + 113, 22, sw=1.2)
        # horse head on the hood, seen from the front: a narrow face with ears
        s += path(f"M{mcx - 14} {by + 90} L{mcx - 12} {by + 56} Q{mcx} {by + 44} {mcx + 12} {by + 56} L{mcx + 14} {by + 90} Z", "url(#brassG)", C["line"], 3)
        s += path(f"M{mcx - 12} {by + 56} L{mcx - 20} {by + 40} L{mcx - 6} {by + 48} Z", "url(#brassG)", C["line"], 2) + path(f"M{mcx + 12} {by + 56} L{mcx + 20} {by + 40} L{mcx + 6} {by + 48} Z", "url(#brassG)", C["line"], 2)
        for sx in (-1, 1): s += circ(mcx + sx * 7, by + 64, 3.5, C["rune"], True, 1.5, 'filter="url(#glow)"')
        s += path(f"M{mcx} {by + 50} L{mcx} {by + 36}", "none", C["gold"], 5)
    else:
        s += line(bx + 12, by + 52, bx + bw - 12, by + 52, C["brass2"], 3.5)
        for k in (0, 1): s += line(bx + 22 + k * (bw - 44), by + 52, bx + 22 + k * (bw - 44), by + 92, C["brass2"], 3)
        s += rect(bx + 8, by + 92, bw - 16, 12, C["wood"], 3, True, 2.5)
        for k in range(3): s += rect(bx + 40, by + 104 + k * 10, bw - 80, 6, C["wood"], 2, True, 2)
        s += rect(bx + 18, by + 14, bw - 36, 30, "url(#glassG)", 6, True, 3)   # rear window
        s += rect(mcx - 22, by + 60, 44, 24, C["red"], 3, True, 2) + triforce(mcx, by + 73, 22, sw=1.2)
        s += line(bx + bw - 24, by - 70, bx + bw - 24, by - 14, C["wood2"], 4) + path(f"M{bx + bw - 24} {by - 70} L{bx + bw + 40} {by - 58} L{bx + bw + 14} {by - 48} L{bx + bw + 40} {by - 36} L{bx + bw - 24} {by - 40} Z", C["red"], C["line"], 2.5)
    for sx in (-1, 1): s += lantern(mcx + sx * 80, by - 4)
    for sx in (-1, 1):
        wx = mcx + sx * 74
        s += rect(wx - 14, by + bh - 20, 28, 26, C["iron"], 3, True, 2.5)
        s += rect(wx - 9, by + bh - 14, 18, 48, C["wood2"], 9, True, 3) + rect(wx - 4, by + bh - 8, 8, 36, C["iron"], 4, False)
    return s

# ------------------------------------------------------------------------------------------------------------------- top view
def top_view(ox, oy):
    bx, by, bw, bh = ox + 150, oy + 300, 470, 140
    s = ""
    # the Moon is lifted off: show the rope ring and sandbags, the roof, the burner cage from above
    s += rect(bx, by, bw, bh, "url(#lacG)", 26, True, 4)
    s += rect(bx + 12, by + 10, bw - 24, bh - 20, C["lac2"], 18, True, 2.5)
    s += rect(bx + 20, by + 18, bw - 40, bh - 36, "url(#lacG)", 14, False)
    for sx in (-1, 1):   # the gold roof rails and the eave lanterns
        s += rect(bx + 20, by + bh/2 + sx * (bh/2 - 14) - 3, bw - 40, 6, C["gold"], 3, True, 1.5)
        for i in range(4): s += circ(bx + 70 + i * 120, by + bh/2 + sx * (bh/2 + 8), 9, C["lantern"], True, 2, 'filter="url(#glow)"')
    mcx, mcy = bx + bw * 0.5, by + bh * 0.5
    s += triforce(bx + 70, mcy, 60, C["gold2"], 2) + triforce(bx + bw - 70, mcy, 60, C["gold2"], 2)   # painted on the roof fore and aft
    s += ell(mcx, mcy, 92, 70, "none", True, 3) + ell(mcx, mcy, 68, 52, "url(#brassG)", True, 3) + ell(mcx, mcy, 58, 44, C["lac2"], False)
    for a in (45, 135, 225, 315): s += circ(mcx + math.cos(math.radians(a)) * 68, mcy + math.sin(math.radians(a)) * 52, 9, C["wood"], True, 2)
    s += circ(mcx, mcy, 26, C["iron"], True, 3) + circ(mcx, mcy, 16, C["flame"], True, 2, 'filter="url(#glow2)"') + circ(mcx, mcy, 8, C["flame2"], False)
    # hood and horse head at the front (left), deck and propeller at the rear (right)
    s += rect(bx - 62, by + 30, 70, 80, "url(#lacG)", 14, True, 3.5)
    s += path(f"M{bx - 40} {by + 58} L{bx - 110} {by + 64} L{bx - 118} {by + 70} L{bx - 110} {by + 76} L{bx - 40} {by + 82} Z", "url(#brassG)", C["line"], 3)
    for k in range(4): s += path(f"M{bx - 50 - k*10} {by + 70} l-6 -8 l-2 16 Z", C["gold"], C["line"], 1.5)
    s += rect(bx + bw - 4, by + 24, 60, 92, C["wood"], 4, True, 2.5)
    for k in range(3): s += line(bx + bw + 12 + k * 16, by + 30, bx + bw + 12 + k * 16, by + 110, C["wood2"], 2)
    hx, hy = bx + bw + 78, by + bh * 0.5
    s += rect(bx + bw + 50, hy - 6, hx - bx - bw - 50, 12, "url(#brassG)", 4, True, 2.5)
    s += rect(hx - 6, hy - 48, 12, 96, C["leaf"], 4, True, 2.5) + ell(hx, hy, 8, 14, "url(#brassG)", True, 2.5)
    # wheels, tucked under the skirt: just their outer edges show
    for wx in (bx + 70, bx + bw - 70):
        for sx in (-1, 1): s += rect(wx - 34, by + bh/2 + sx * (bh/2 + 4) - 6, 68, 12, C["wood2"], 5, True, 2.5)
    # banner
    s += path(f"M{bx + bw - 12} {by + bh - 4} L{bx + bw + 70} {by + bh + 20} L{bx + bw + 40} {by + bh + 26} L{bx + bw + 70} {by + bh + 40} L{bx + bw - 12} {by + bh + 14} Z", C["red"], C["line"], 2.5)
    s += text(bx + bw * 0.5, oy + 70, "The Moon lifted off: rope ring, sandbags, burner cage and firebox on the roof", 15, "#3A2A1A", "middle", italic=True)
    # the moon's footprint
    s += circ(mcx, mcy, 150, "none", True, 2, 'stroke-dasharray="8 8"')
    s += text(mcx, mcy + 172, "dashed: the Moon's footprint, 1050 units across", 14, "#3A2A1A", "middle")
    return s

# ------------------------------------------------------------------------------------------------------------------- the sheet
def panel(ox, oy, title, body, notes):
    s = f'<rect x="{ox}" y="{oy}" width="{PW}" height="{PH}" fill="url(#sky)" stroke="#3A2A1A" stroke-width="2"/>'
    s += text(ox + 18, oy + 34, title, 26, "#2A1408", weight="bold")
    s += body
    for i, n in enumerate(notes): s += text(ox + 18, oy + PH - 18 - (len(notes) - 1 - i) * 22, n, 15, "#2A1408")
    return s

side = side_view(0, 0)
side_callouts = (
    callout(425 - 60, 130, 885, 60, "The Moon (Majora's Mask): cratered rock, glowing eyes, grin", "end") +
    callout(425 + 26, 395, 885, 372, "Goron firebox in a brass cage; its flame feeds the Moon", "end") +
    callout(425 + 190, 534, 885, 600, "Sheikah rune line, glowing cyan", "end") +
    callout(150, 520, 40, 620, "Bronze horse-head prow") +
    callout(262, 588, 40, 650, "Spoked cart wheels, Sheikah eye hubs") +
    callout(738, 495, 560, 680, "Deku-leaf propeller") +
    callout(690, 547, 885, 650, "Rear deck and boarding steps", "end") +
    callout(330, 492, 90, 395, "Seven arched brass windows a side"))
front = end_view(900, 0, True); rear = end_view(0, 760, False); top = top_view(900, 760)
svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{PW*2}" height="{PH*2}" font-family="Georgia, 'DejaVu Serif', serif">{DEFS}
{panel(0, 0, "The Moonfall Bus: side", side + side_callouts, ["Body 1100 x 320 x 300 units, lacquered coach with gold bands; 32 seats inside behind the window band.", "Scale: adult Link 68 units; the window sill is at 230, seats look out over it."])}
{panel(900, 0, "Front: the Moon looks down at the island", front, ["The pilot's bay is open glass; the Goron pilot sits at a brass tiller.", "Cyan lamps and horse-head eye glow at night, like the runes and the Moon's eyes."])}
{panel(0, 760, "Rear: propeller, deck, banner", rear, ["Four Deku leaves on a brass hub, 450 across, spinning. Rail and three steps for boarding in the lobby.", "The Moon's back is plain cratered rock in a rope net, ending in a collar at its mouth."])}
{panel(900, 760, "Top", top, ["Burner cage ring is where the four ropes tie; sandbags at the corners.", "Far LOD past 4500 units: the Moon as a 12x6 sphere, the body as a box."])}
</svg>'''
html = f"<html><body style='margin:0;background:#fff'>{svg}</body></html>"
tmp = os.path.join(os.path.dirname(os.path.abspath(OUT)), "concept_sheet.html")
open(tmp, "w").write(html)
open(os.path.splitext(OUT)[0] + ".svg", "w").write(svg)
print("svg ok")
