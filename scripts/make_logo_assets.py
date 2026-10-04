#!/usr/bin/env python3
"""Turn assets/logo.png into everything that shows the logo.

  * the Android launcher icons (adaptive foreground plus the old square and round ones, webp, five sizes) -> assets/generated/res
  * the in-game copy of the logo (title screen, menus): a 256-colour, run-length-coded picture in a C++ header -> mod/Royale/logo_data.h

Run by scripts/apply_logo.sh (and by the build). A logo with a white background is cut out of it automatically. Needs Pillow.
Usage: make_logo_assets.py [--placeholder]    (--placeholder draws a stand-in logo into assets/logo.png first, if there is none)
"""
import os, sys
from collections import deque
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LOGO = os.path.join(ROOT, "assets", "logo.png")
RES = os.path.join(ROOT, "assets", "generated", "res")
HEADER = os.path.join(ROOT, "mod", "Royale", "logo_data.h")
BACKGROUND = (246, 241, 229)  # the launcher icon's backdrop: warm off-white (the logo's lettering is black, so it needs a light backdrop)


def placeholder():
    img = Image.new("RGBA", (900, 700), (255, 255, 255, 0))
    d = ImageDraw.Draw(img)
    try:
        big = ImageFont.truetype("DejaVuSans-Bold.ttf", 150)
        small = ImageFont.truetype("DejaVuSans-Bold.ttf", 64)
    except OSError:
        big = small = ImageFont.load_default()
    d.polygon([(450, 40), (560, 230), (340, 230)], fill=(246, 206, 60))
    d.text((450, 380), "OOT", font=big, fill=(214, 40, 40), anchor="mm")
    d.text((450, 540), "BATTLE ROYALE", font=small, fill=(214, 40, 40), anchor="mm")
    img.save(LOGO)


def cut_out_white(img):
    """If the picture has no transparency, make the white that touches its border transparent."""
    img = img.convert("RGBA")
    px = img.load()
    w, h = img.size
    if any(px[x, y][3] < 250 for x, y in ((0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1))):
        return img                                        # already has a transparent border
    near_white = lambda p: p[0] > 244 and p[1] > 244 and p[2] > 244
    seen = bytearray(w * h)
    q = deque()
    for x in range(w):
        for y in (0, h - 1):
            q.append((x, y))
    for y in range(h):
        for x in (0, w - 1):
            q.append((x, y))
    while q:
        x, y = q.popleft()
        if x < 0 or y < 0 or x >= w or y >= h or seen[y * w + x]:
            continue
        if not near_white(px[x, y]):
            continue
        seen[y * w + x] = 1
        px[x, y] = (255, 255, 255, 0)
        q.extend(((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)))
    # soften the fringe: a pixel next to the cut-out that is almost white becomes half transparent
    for y in range(1, h - 1):
        for x in range(1, w - 1):
            if px[x, y][3] == 255 and near_white((px[x, y][0] + 12, px[x, y][1] + 12, px[x, y][2] + 12)) and (
                    px[x + 1, y][3] == 0 or px[x - 1, y][3] == 0 or px[x, y + 1][3] == 0 or px[x, y - 1][3] == 0):
                px[x, y] = (px[x, y][0], px[x, y][1], px[x, y][2], 120)
    return img


def fit(img, box, bg=None):
    """The logo scaled to fit a w x h box (keeping its shape), centred."""
    w, h = box
    scale = min(w / img.width, h / img.height)
    small = img.resize((max(1, round(img.width * scale)), max(1, round(img.height * scale))), Image.LANCZOS)
    canvas = Image.new("RGBA", (w, h), bg if bg else (0, 0, 0, 0))
    canvas.alpha_composite(small, ((w - small.width) // 2, (h - small.height) // 2))
    return canvas


def android_icons(logo):
    sizes = {"mdpi": 48, "hdpi": 72, "xhdpi": 96, "xxhdpi": 144, "xxxhdpi": 192}
    for dens, px in sizes.items():
        out = os.path.join(RES, "mipmap-" + dens)
        os.makedirs(out, exist_ok=True)
        fg = px * 108 // 48          # the adaptive icon is 108dp, the visible part the middle 72dp
        # foreground: the logo inside the safe zone, transparent around it
        layer = fit(logo, (round(fg * 0.64), round(fg * 0.64)))
        canvas = Image.new("RGBA", (fg, fg), (0, 0, 0, 0))
        canvas.alpha_composite(layer, ((fg - layer.width) // 2, (fg - layer.height) // 2))
        canvas.save(os.path.join(out, "ic_launcher_foreground.webp"), "WEBP", quality=95, method=6)
        # the old-style icons: the logo on the backdrop, as a rounded square and as a circle
        base = Image.new("RGBA", (px, px), BACKGROUND + (255,))
        inner = fit(logo, (round(px * 0.82), round(px * 0.82)))
        base.alpha_composite(inner, ((px - inner.width) // 2, (px - inner.height) // 2))
        for name, shape in (("ic_launcher", "round_rect"), ("ic_launcher_round", "circle")):
            mask = Image.new("L", (px * 4, px * 4), 0)
            md = ImageDraw.Draw(mask)
            if shape == "circle":
                md.ellipse((0, 0, px * 4 - 1, px * 4 - 1), fill=255)
            else:
                md.rounded_rectangle((0, 0, px * 4 - 1, px * 4 - 1), radius=px * 4 // 5, fill=255)
            mask = mask.resize((px, px), Image.LANCZOS)
            icon = Image.new("RGBA", (px, px), (0, 0, 0, 0))
            icon.paste(base, (0, 0), mask)
            icon.save(os.path.join(out, name + ".webp"), "WEBP", quality=95, method=6)
    values = os.path.join(RES, "values")
    os.makedirs(values, exist_ok=True)
    with open(os.path.join(values, "ic_launcher_background.xml"), "w") as f:
        f.write('<?xml version="1.0" encoding="utf-8"?>\n<resources>\n    <color name="ic_launcher_background">#%02X%02X%02X</color>\n</resources>\n' % BACKGROUND)


def header(logo):
    width = 480
    h = max(1, round(logo.height * width / logo.width))
    small = logo.resize((width, h), Image.LANCZOS)
    # a 256-colour version with a transparent colour: small enough to live in the source code
    alpha = small.getchannel("A")
    rgb = Image.new("RGB", small.size, (0, 0, 0))
    rgb.paste(small.convert("RGB"), (0, 0), alpha)
    pal = rgb.quantize(colors=255, method=Image.MEDIANCUT, dither=Image.NONE)
    palette = pal.getpalette()[: 255 * 3]
    idx = list(pal.getdata())
    data = [i + 1 if alpha.getpixel((n % width, n // width)) > 40 else 0 for n, i in enumerate(idx)]   # index 0 = transparent
    soft = [alpha.getpixel((n % width, n // width)) for n in range(len(data))]
    runs = []
    n = 0
    while n < len(data):
        v, a, c = data[n], soft[n] if data[n] else 0, 1
        while n + c < len(data) and c < 255 and data[n + c] == v and (soft[n + c] if data[n + c] else 0) == a:
            c += 1
        runs.append((c, v, a))
        n += c
    out = ["// Generated by scripts/make_logo_assets.py from assets/logo.png. Do not edit.",
           "#pragma once", "#include <cstdint>", "namespace royale {",
           "constexpr int kLogoW = %d, kLogoH = %d;" % (width, h),
           "// 255 colours (index 1 to 255; 0 is clear), then runs of (count, colour, opacity)."]
    out.append("inline const uint8_t kLogoPalette[255 * 3] = {" + ",".join(str(v) for v in palette) + "};")
    out.append("constexpr int kLogoRuns = %d;" % len(runs))
    out.append("inline const uint8_t kLogoRle[kLogoRuns * 3] = {" + ",".join("%d,%d,%d" % r for r in runs) + "};")
    out.append("} // namespace royale")
    with open(HEADER, "w") as f:
        f.write("\n".join(out) + "\n")
    return len(runs)


def main():
    if "--placeholder" in sys.argv and not os.path.exists(LOGO):
        placeholder()
    if not os.path.exists(LOGO):
        print("no assets/logo.png: nothing to do")
        return
    logo = cut_out_white(Image.open(LOGO))
    box = logo.getbbox()
    if box:
        logo = logo.crop(box)
    android_icons(logo)
    runs = header(logo)
    print("logo %dx%d -> icons in assets/generated/res, %d runs in mod/Royale/logo_data.h" % (logo.width, logo.height, runs))


main()
