#!/usr/bin/env python3
# Erzeugt die Bedienelemente des SC-8850-Panels aus den Original-Grafiken des 88emu-Players
# (Gearmulator, The Usual Suspects, GPLv3): Tasten/LEDs/PREVIEW aus sc8850_assets.png, GAIN-Regler aus
# knob.png (31 Stellungen), VALUE-Drehgeber aus value_knob.png (4 Riffelungsphasen). Lage und Groesse
# wie im Player-Skin (emu88Player.rcss, dp), Panel = dp x 1,5 (918x280). Die Tasten liegen teils auf
# halben Pixeln; deshalb gibt es jede Tastengrafik in 4 Subpixel-Phasen, je normal und gedrueckt
# (Player: image-color #bbb).
# Ausgabe: vormultiplizierte BGRA-Pixel in fester Reihenfolge (fuer AlphaBlend):
#   4 Phasen (fx, fy je 0/0,5 px) x [BUTTON, LED_OFF, LED_ON] x [normal, gedrueckt], je 38x38
#     (Ursprung = floor(1,5 * Tasten-Links/Oben) - 4)
#   PREVIEW normal + gedrueckt, je 44x44 (Ursprung 169, 111)
#   GAIN 31 x 42x42 (Ursprung 168, 36)
#   VALUE 4 x 116x116 (Ursprung 775, 25)
# usage: make_sc8850_sprites.py <88emuplayer/assets> <out.bgra>
import sys
from PIL import Image
A, out = sys.argv[1].rstrip('/') + '/', sys.argv[2]
SS = 8  # Ueberabtastung fuer die Subpixel-Lage

sheet = Image.open(A + 'sc8850_assets.png').convert('RGBA')
def sprite(rect):
    x, y, w, h = rect
    return sheet.crop((x, y, x + w, y + h))
BUTTON, LED_OFF, LED_ON = (108, 66, 50, 51), (14, 159, 51, 49), (96, 149, 63, 64)
PREVIEW = (0, 54, 78, 79)
# Lage relativ zur 17x17-dp-Taste (left, top, width, height in dp)
GEOM = {BUTTON: (2, 0.6, 16.667, 17), LED_OFF: (1.667, 1.2, 17, 16.333), LED_ON: (-2, -3.133, 21, 21.333)}

def place(img, canvas, x, y, w, h):
    """img (contain) in einem canvas x canvas Feld an der Pixelposition x, y mit Groesse w x h."""
    big = Image.new('RGBA', (canvas * SS, canvas * SS), (0, 0, 0, 0))
    # "contain": Seitenverhaeltnis bleibt, zentriert im Kasten
    s = min(w / img.width, h / img.height)
    iw, ih = img.width * s, img.height * s
    x += (w - iw) / 2; y += (h - ih) / 2
    part = img.resize((max(1, round(iw * SS)), max(1, round(ih * SS))), Image.LANCZOS)
    big.alpha_composite(part, (round(x * SS), round(y * SS)))
    return big.resize((canvas, canvas), Image.BOX)

def tint(im, f=0xbb / 255):
    r, g, b, a = im.split()
    return Image.merge('RGBA', [c.point(lambda v: round(v * f)) for c in (r, g, b)] + [a])

def bgra_premul(im):
    d = im.tobytes(); b = bytearray(len(d))
    for i in range(0, len(d), 4):
        r, g, bl, a = d[i], d[i + 1], d[i + 2], d[i + 3]
        b[i:i + 4] = bytes((bl * a // 255, g * a // 255, r * a // 255, a))
    return b

blob = bytearray()
for ph in range(4):
    fx, fy = 0.5 * (ph & 1), 0.5 * (ph >> 1)
    for spr in (BUTTON, LED_OFF, LED_ON):
        ox, oy, w, h = GEOM[spr]
        im = place(sprite(spr), 38, 4 + fx + 1.5 * ox, 4 + fy + 1.5 * oy, 1.5 * w, 1.5 * h)
        blob += bgra_premul(im) + bgra_premul(tint(im))
# PREVIEW: Taste 114/76 dp (24x25), Grafik +0,5/-0,3 dp, 26x26,333 dp; Feld ab Pixel (169, 111)
im = place(sprite(PREVIEW), 44, 1.5 * 114.5 - 169, 1.5 * 75.7 - 111, 1.5 * 26, 1.5 * 26.333)
blob += bgra_premul(im) + bgra_premul(tint(im))
knob = Image.open(A + 'knob.png').convert('RGBA')
for f in range(31):
    blob += bgra_premul(knob.crop((0, f * 128, 128, f * 128 + 128)).resize((42, 42), Image.LANCZOS))
vk = Image.open(A + 'value_knob.png').convert('RGBA')
for f in range(4):  # 517/17 dp, 76x76 dp -> 775,5/25,5 px, 114 px
    blob += bgra_premul(place(vk.crop((0, f * 128, 128, f * 128 + 128)), 116, 0.5, 0.5, 114, 114))
open(out, 'wb').write(blob)
print(f"{out}: {len(blob)} Bytes")
