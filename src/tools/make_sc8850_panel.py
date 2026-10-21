#!/usr/bin/env python3
# Prepares the 88emu panel graphic of the SC-8850 (sc8850_panel.png, GPLv3, The Usual Suspects) for the
# Emu-SC8850 poly panel: half the size of the 3x export (918x280 = 612x187 dp x 1.5), branding
# replaced, playlist header -> VOICES, VOLUME -> GAIN; everything else stays unchanged.
# Output: raw BGRA pixels (918*280*4 bytes) for .incbin.
# usage: make_sc8850_panel.py <sc8850_panel.png> <out.bgra> [preview.png]
import sys
from PIL import Image, ImageDraw, ImageFont
src, out = sys.argv[1], sys.argv[2]
im = Image.open(src).convert("RGB")
im = im.resize((918, 280), Image.LANCZOS)
d = ImageDraw.Draw(im)
DEJAVU = "/usr/share/fonts/truetype/dejavu/DejaVuSans"
def font(sz, style=""):
    try: return ImageFont.truetype(DEJAVU + style + ".ttf", sz)
    except OSError: return ImageFont.load_default()

def hgrad_fill(box, lx, rx):
    """Smooth area with a horizontal gradient (black LCD frame): per row, linear between
    the pixels at x=lx and x=rx."""
    x0, y0, x1, y1 = box; p = im.load()
    for y in range(y0, y1):
        a, b = p[lx, y], p[rx, y]
        for x in range(x0, x1):
            t = (x - lx) / (rx - lx)
            p[x, y] = tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))

def texture_fill(box, src, feather=4, sides="lrtb"):
    """Grained area: take the texture of the same rows from the lettering-free column range src=(a, b),
    tiled mirrored (no seam) and blended in softly at the edges."""
    x0, y0, x1, y1 = box; sa, sb = src; w = sb - sa; p = im.load()
    orig = {(x, y): p[x, y] for x in range(x0, x1) for y in range(y0, y1)}
    for y in range(y0, y1):
        for x in range(x0, x1):
            k, r = divmod(x - x0, w)
            sx = sa + (r if k % 2 == 0 else w - 1 - r)
            c = p[sx, y] if not (x0 <= sx < x1) else orig[(sx, y)]
            e = min([d_ for d_, sd in ((x - x0, "l"), (x1 - 1 - x, "r"), (y - y0, "t"), (y1 - 1 - y, "b"))
                     if sd in sides] or [feather])
            if e < feather:
                t = (e + 1) / (feather + 1); o = orig[(x, y)]
                c = tuple(round(o[i] + (c[i] - o[i]) * t) for i in range(3))
            p[x, y] = c

def inpaint_text(box, base_cols, thr=5, grow=2):
    """Smooth area with lettering/symbol: only deviating or coloured pixels (with their fringe) are
    filled in vertically, linearly between the nearest untouched neighbours."""
    x0, y0, x1, y1 = box; p = im.load()
    lum = lambda c: 0.3 * c[0] + 0.59 * c[1] + 0.11 * c[2]
    mask = set()
    for y in range(y0, y1):
        base = sorted(lum(p[x, y]) for x in range(*base_cols))[(base_cols[1] - base_cols[0]) // 2]
        for x in range(x0, x1):
            c = p[x, y]
            if abs(lum(c) - base) > thr or max(c) - min(c) > 40:
                for dx in range(-grow, grow + 1):
                    for dy in range(-grow, grow + 1):
                        if x0 <= x + dx < x1 and y0 <= y + dy < y1: mask.add((x + dx, y + dy))
    for x in range(x0, x1):
        y = y0
        while y < y1:
            if (x, y) not in mask: y += 1; continue
            a = y
            while y < y1 and (x, y) in mask: y += 1
            t_ = p[x, a - 1]; b_ = p[x, y] if y < im.height else t_
            for k in range(a, y):
                t = (k - a + 1) / (y - a + 1)
                p[x, k] = tuple(round(t_[i] + (b_[i] - t_[i]) * t) for i in range(3))

def oblique_text(x, y, t, f, fill, slant=0.21):
    """Italic like the original: render the lettering upright and shear it (only DejaVu Sans needed)."""
    l, top, r, b = d.textbbox((0, 0), t, font=f)
    w, h = r + 8, b + 4
    m = Image.new("L", (w, h), 0); ImageDraw.Draw(m).text((2, 0), t, font=f, fill=255)
    m = m.transform((w, h), Image.AFFINE, (1, slant, -slant * h, 0, 1, 0), resample=Image.BICUBIC)
    im.paste(Image.new("RGB", (w, h), fill), (int(x), int(y)), m)
    return d.textlength(t, font=f)

def center_text(cx, y, t, f, fill):
    w = d.textlength(t, font=f); d.text((cx - w / 2, y), t, font=f, fill=fill)

# Branding on top of the LCD frame ("TheUsualSuspects", white italic) -> product name, same style
hgrad_fill((408, 6, 571, 30), 406, 572)
f1, f2 = font(15), font(15, "-Bold")
w = d.textlength("Emu-", font=f1) + d.textlength("SC8850", font=f2)
x = 566 - w
x += oblique_text(x, 9, "Emu-", f1, (236, 238, 240))
oblique_text(x, 9, "SC8850", f2, (236, 238, 240))
# Logo at the bottom left ("88Emu50" + arrow): texture from the empty area to its right (same rows)
# (template between the holes of F1 and F2, tiled mirrored)
texture_fill((30, 234, 226, 274), (298, 328))
f1, f2 = font(22), font(22, "-Bold")
x = 40 + oblique_text(40, 239, "Emu-SC", f1, (22, 22, 22))
oblique_text(x, 239, "8850", f2, (22, 22, 22))
# playlist header -> "VOICES" (folder symbol removed), column gradient stays
inpaint_text((32, 13, 141, 34), (40, 135))
center_text(86, 17, "VOICES", font(11, "-Bold"), (28, 28, 28))
# Level knob: "VOLUME" -> "GAIN"; texture from the lettering-free area to its right
texture_fill((158, 15, 218, 33), (218, 234), feather=2)
center_text(189.5, 18, "GAIN", font(11, "-Bold"), (28, 28, 28))
px = im.load(); w, h = im.size
raw = bytearray(w * h * 4); i = 0
for y in range(h):
    for x in range(w):
        r, g, b = px[x, y]; raw[i:i + 4] = bytes((b, g, r, 255)); i += 4
open(out, "wb").write(raw)
if len(sys.argv) > 3: im.save(sys.argv[3])
print(f"{out}: {w}x{h}")
