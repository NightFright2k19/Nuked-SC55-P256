#!/usr/bin/env python3
# Bereitet die 88emu-Panelgrafik (sc88pro_panel.png, GPLv3, The Usual Suspects) fuer das
# Nuked-SC88-Pro-Poly-Panel auf: halbe Groesse (918x280), Branding und EFX-Beschriftungen
# ersetzt, Playlist-Kopf ersetzt, VOLUME -> GAIN. Ausgabe: rohe BGRA-Pixel (918*280*4 Byte) fuer .incbin.
# usage: make_sc88_panel.py <sc88pro_panel.png> <out.bgra> [preview.png]
import sys
from PIL import Image, ImageDraw, ImageFont
src, out = sys.argv[1], sys.argv[2]
im = Image.open(src).convert("RGB")
im = im.resize((im.width // 2, im.height // 2), Image.LANCZOS)
d = ImageDraw.Draw(im)
def font(sz, bold=True):
    for f in ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf" if bold else "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",):
        try: return ImageFont.truetype(f, sz)
        except OSError: pass
    return ImageFont.load_default()
def patch(box, lx, rx):
    """Glatter Bereich mit senkrechtem Verlauf: je Zeile Mittel der Bildpunkte bei x=lx und x=rx."""
    x0, y0, x1, y1 = box; p = im.load()
    for y in range(y0, y1):
        a, b = p[lx, y], p[rx, y]
        c = tuple((a[i] + b[i]) // 2 for i in range(3))
        for x in range(x0, x1): p[x, y] = c

def smooth_fill(box, anchors):
    """Glatte Flaeche ohne Koernung (z. B. PLAYLIST-Kopf): je Zeile stueckweise linear zwischen den
    Median-Farben schriftfreier Ankerspalten-Bereiche [(x_von, x_bis), ...] interpolieren."""
    x0, y0, x1, y1 = box; p = im.load()
    for y in range(y0, y1):
        pts = []
        for a, b in anchors:
            cols = sorted(p[x, y] for x in range(a, b))
            pts.append(((a + b - 1) / 2, cols[len(cols) // 2]))
        for x in range(x0, x1):
            if x <= pts[0][0]: c = pts[0][1]
            elif x >= pts[-1][0]: c = pts[-1][1]
            else:
                k = max(i for i in range(len(pts) - 1) if pts[i][0] <= x)
                (xa, ca), (xb, cb) = pts[k], pts[k + 1]; t = (x - xa) / (xb - xa)
                c = tuple(round(ca[i] + (cb[i] - ca[i]) * t) for i in range(3))
            p[x, y] = c

def texture_fill(box, src, feather=4, sides="lrtb"):
    """Gekoernte Flaeche: Struktur derselben Zeilen aus dem schriftfreien Spaltenbereich src=(a, b)
    uebernehmen, gespiegelt gekachelt (keine Naht) und an den Raendern weich eingeblendet. Nur an den
    in `sides` genannten Raendern (l, r, t, b) wird ueberblendet - dort darf keine Schrift liegen."""
    x0, y0, x1, y1 = box; sa, sb = src; w = sb - sa; p = im.load()
    orig = {(x, y): p[x, y] for x in range(x0, x1) for y in range(y0, y1)}
    for y in range(y0, y1):
        for x in range(x0, x1):
            k, r = divmod(x - x0, w)
            sx = sa + (r if k % 2 == 0 else w - 1 - r)          # Spiegelkachel
            c = p[sx, y] if not (x0 <= sx < x1) else orig[(sx, y)]
            e = min([d_ for d_, sd in ((x - x0, "l"), (x1 - 1 - x, "r"), (y - y0, "t"), (y1 - 1 - y, "b"))
                     if sd in sides] or [feather])               # Abstand zum Rand
            if e < feather:
                t = (e + 1) / (feather + 1); o = orig[(x, y)]
                c = tuple(round(o[i] + (c[i] - o[i]) * t) for i in range(3))
            p[x, y] = c

def inpaint_text(box, base_cols, thr=5, grow=2):
    """Glatte Flaeche mit Schrift/Symbol (z. B. PLAYLIST-Kopf): Original-Pixel bleiben unveraendert.
    Nur Pixel, die vom Zeilen-Grundton (Median ueber base_cols) abweichen oder farbig sind, werden
    samt Saum (grow) senkrecht zwischen den naechsten unberuehrten Nachbarn linear aufgefuellt."""
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
    # Senkrecht auffuellen: je Spalte zwischen den naechsten unberuehrten Pixeln darueber und darunter.
    # Jede Spalte behaelt so ihren eigenen Ton (auch die helleren Randzonen an den Fasen).
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

def center_text(cx, y, t, f, fill):
    w = d.textlength(t, font=f); d.text((cx - w / 2, y), t, font=f, fill=fill)

# Branding oben ("TheUsualSuspects") -> Produktname (Verlauf/helle Oberkante bleiben)
patch((388, 2, 562, 29), 386, 563)
center_text(475, 8, "NUKED-SC88 PRO", font(13), (235, 238, 242))
# Logo unten ("88EmuPro" + Pfeil): Struktur aus der leeren Flaeche rechts daneben (gleiche Zeilen)
texture_fill((22, 238, 218, 272), (222, 420))
d.text((34, 244), "Nuked-SC88", font=font(17, False), fill=(235, 238, 242))
d.text((34 + d.textlength("Nuked-SC88 ", font=font(17, False)), 244), "Pro", font=font(17), fill=(242, 108, 32))
# Playlist-Kopf -> "VOICES": glatt interpoliert zwischen schriftfreien Stellen, Fasen bleiben unberuehrt
inpaint_text((31, 5, 139, 35), (40, 130))
center_text(85.5, 13, "VOICES", font(11), (235, 238, 242))
# EFX-Bereich: alte Beschriftungen (bis Zeile 240) mit Struktur aus der leeren Flaeche links daneben;
# Zeile 246 (Schattenkante der Fenster) bleibt unberuehrt
texture_fill((452, 197, 892, 246), (200, 415), feather=6, sides="lr")  # Vorlage endet vor der Buchse (x~430); rechts weich in den Randverlauf (Schrift endet bei x=881)
# Weisser Winkel rechts neben dem SETUP-Fenster (ohne Funktion): Struktur aus der Luecke zwischen
# TONE MAP und UNITS (gleiche Zeilen); die Fensterkanten bleiben stehen
texture_fill((552, 241, 580, 259), (677, 692), feather=2, sides="rb")
# Pegelknopf: "VOLUME" -> "GAIN" (Issue #3); Struktur aus der schriftfreien Flaeche rechts daneben
texture_fill((161, 15, 219, 29), (219, 228), feather=2)
center_text(189.5, 16, "GAIN", font(12, False), (226, 230, 236))
# Bezeichner auf den orangen Feldern (schwarz), Felder vorher leeren
for (x0, x1), t in zip(((464, 551), (585, 673), (695, 782), (804, 892)), ("SETUP", "TONE MAP", "UNITS", "MAX VOICES")):
    d.rectangle((x0 + 2, 266, x1 - 2, 275), fill=(244, 106, 28))
    center_text((x0 + x1) / 2, 266, t, font(8), (0, 0, 0))
px = im.load(); w, h = im.size
raw = bytearray(w * h * 4); i = 0
for y in range(h):
    for x in range(w):
        r, g, b = px[x, y]; raw[i:i + 4] = bytes((b, g, r, 255)); i += 4
open(out, "wb").write(raw)
if len(sys.argv) > 3: im.save(sys.argv[3])
print(f"{out}: {w}x{h}")
