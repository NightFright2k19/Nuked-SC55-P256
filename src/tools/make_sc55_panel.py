#!/usr/bin/env python3
# Bereitet die 88emu-Panelgrafik des SC-55 / SC-55mkII (sc55_panel.png / sc55mk2_panel.png, GPLv3,
# The Usual Suspects) fuer das Nuked-SC55-Panel auf: 918x280 (= 612x187 dp x 1,5), Branding ersetzt, Playlist-Kopf ->
# VOICES, VOLUME -> GAIN, Logo unten links ersetzt; alles andere bleibt unveraendert.
# Ausgabe: rohe BGRA-Pixel (918*280*4 Byte) fuer .incbin.
# usage: make_sc55_panel.py <sc55_panel.png|sc55mk2_panel.png> <mk1|mk2> <out.bgra> [preview.png]
import sys
from PIL import Image, ImageDraw, ImageFont
src, model, out = sys.argv[1], sys.argv[2], sys.argv[3]
mk2 = model == "mk2"
im = Image.open(src).convert("RGB")
im = im.resize((918, 280), Image.LANCZOS)
d = ImageDraw.Draw(im)
DEJAVU = "/usr/share/fonts/truetype/dejavu/DejaVuSans"
def font(sz, style=""):
    try: return ImageFont.truetype(DEJAVU + style + ".ttf", sz)
    except OSError: return ImageFont.load_default()

def hgrad_fill(box, lx, rx):
    """Glatte Flaeche mit waagerechtem Verlauf (schwarzer LCD-Rahmen): je Zeile linear zwischen
    den Bildpunkten bei x=lx und x=rx."""
    x0, y0, x1, y1 = box; p = im.load()
    for y in range(y0, y1):
        a, b = p[lx, y], p[rx, y]
        for x in range(x0, x1):
            t = (x - lx) / (rx - lx)
            p[x, y] = tuple(round(a[i] + (b[i] - a[i]) * t) for i in range(3))

def texture_fill(box, src, feather=4, sides="lrtb"):
    """Gekoernte Flaeche: Struktur derselben Zeilen aus dem schriftfreien Spaltenbereich src=(a, b)
    uebernehmen, gespiegelt gekachelt (keine Naht) und an den Raendern weich eingeblendet."""
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
    """Glatte Flaeche mit Schrift/Symbol: nur abweichende oder farbige Pixel (samt Saum) werden
    senkrecht zwischen den naechsten unberuehrten Nachbarn linear aufgefuellt."""
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
    """Kursiv wie im Original: Schrift aufrecht rendern und scheren (nur DejaVu Sans noetig)."""
    l, top, r, b = d.textbbox((0, 0), t, font=f)
    w, h = r + 8, b + 4
    m = Image.new("L", (w, h), 0); ImageDraw.Draw(m).text((2, 0), t, font=f, fill=255)
    m = m.transform((w, h), Image.AFFINE, (1, slant, -slant * h, 0, 1, 0), resample=Image.BICUBIC)
    im.paste(Image.new("RGB", (w, h), fill), (int(x), int(y)), m)
    return d.textlength(t, font=f)

def center_text(cx, y, t, f, fill):
    w = d.textlength(t, font=f); d.text((cx - w / 2, y), t, font=f, fill=fill)

def vgrad_fill(box, lx, rx):
    """Glatte Flaeche mit senkrechtem Verlauf (Rahmen ueber dem LCD): je Zeile Mittel der
    Bildpunkte bei x=lx und x=rx."""
    x0, y0, x1, y1 = box; p = im.load()
    for y in range(y0, y1):
        a, b = p[lx, y], p[rx, y]
        c = tuple((a[i] + b[i]) // 2 for i in range(3))
        for x in range(x0, x1): p[x, y] = c

# Branding oben auf dem LCD-Rahmen ("TheUsualSuspects", weiss kursiv) -> Produktname, gleicher Stil
vgrad_fill((390, 6, 558, 29), 388, 560)
f1, f2 = font(15), font(15, "-Bold")
name = "SC-55mkII" if mk2 else "SC-55"
w = d.textlength("Nuked", font=f1) + d.textlength(name, font=f2)
x = 551 - w
x += oblique_text(x, 9, "Nuked", f1, (236, 238, 240))
oblique_text(x, 9, name, f2, (236, 238, 240))
# Logo unten links ("55Emu" / "55EmumkII" + Pfeil): Struktur aus der leeren Flaeche rechts daneben
texture_fill((32, 236, 225, 274), (235, 385))
f1, f2 = font(22), font(22, "-Bold")
x = 42 + oblique_text(42, 241, "Nuked-SC", f1, (236, 238, 240))
x += oblique_text(x, 241, "55", f2, (236, 238, 240))
if mk2: oblique_text(x + 2, 241, "mkII", f1, (240, 120, 40))
# Playlist-Kopf -> "VOICES" (Ordnersymbol entfernt), Spalten-Verlauf bleibt
inpaint_text((31, 12, 140, 35), (95, 115), thr=3, grow=2)
center_text(86, 18, "VOICES", font(11, "-Bold"), (226, 228, 232))
# Pegelknopf: "VOLUME" -> "GAIN"; Struktur aus der schriftfreien Flaeche rechts daneben
texture_fill((160, 15, 219, 30), (219, 229), feather=2)
center_text(189.5, 17, "GAIN", font(12), (226, 230, 236))
px = im.load(); w, h = im.size
raw = bytearray(w * h * 4); i = 0
for y in range(h):
    for x in range(w):
        r, g, b = px[x, y]; raw[i:i + 4] = bytes((b, g, r, 255)); i += 4
open(out, "wb").write(raw)
if len(sys.argv) > 4: im.save(sys.argv[4])
print(f"{out}: {w}x{h}")
