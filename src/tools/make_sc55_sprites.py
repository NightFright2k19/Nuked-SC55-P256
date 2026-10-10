#!/usr/bin/env python3
# Erzeugt die Bedienelemente des SC-55-/SC-55mkII-Panels aus den Original-Grafiken des 88emu-Players
# (Gearmulator, The Usual Suspects, GPLv3): Tasten, Wippen aus den SVGs, GAIN-Regler aus knob.png
# (31 Stellungen). Lage und Groesse wie im Player-Skin (emu88Player.rml/.rcss, Klasse modelLegacy,
# dp), Panel = dp x 1,5 (918x280). Jede Grafik wird an ihrer eigenen (Subpixel-)Lage gerendert
# (8-fach ueberabgetastet), mit Rand fuer Schatten/Leuchten ausserhalb der SVG-Box.
# Gedrueckt: Player-Toenung image-color #bbb (LED-Bild #ccc).
# Ausgabe: je Grafik int16 x, int16 y, uint16 w, uint16 h (little endian), dann w*h
# vormultiplizierte BGRA-Pixel (fuer AlphaBlend), in fester Reihenfolge:
#   0..7    ALL, MUTE: je [aus, aus gedrueckt, an, an gedrueckt]
#   8..11   PART <, PART >: je [normal, gedrueckt]
#   12..39  7 Wippen (INSTRUMENT, LEVEL, PAN, REVERB, CHORUS, KEY SHIFT, MIDI CH):
#           je links [normal, gedrueckt], rechts [normal, gedrueckt]
#   40..70  GAIN 31 Stellungen
# usage: make_sc55_sprites.py <88emuplayer/assets> <out.bgra>
import io, math, re, struct, sys
import cairosvg
from PIL import Image
A, out = sys.argv[1].rstrip('/') + '/', sys.argv[2]
SS, K, MARGIN = 8, 1.5, 4.0  # Ueberabtastung, px je dp, Rand in dp

def tint(im, f):
    r, g, b, a = im.split()
    return Image.merge('RGBA', [c.point(lambda v: round(v * f)) for c in (r, g, b)] + [a])

def bgra_premul(im):
    d = im.tobytes(); b = bytearray(len(d))
    for i in range(0, len(d), 4):
        r, g, bl, a = d[i], d[i + 1], d[i + 2], d[i + 3]
        b[i:i + 4] = bytes((bl * a // 255, g * a // 255, r * a // 255, a))
    return b

def render(name, left, top, w, h):
    """SVG (Box w x h dp an left/top dp, gestreckt) samt Rand -> (x0, y0, Bild) in Panel-Pixeln."""
    src = open(A + name, encoding='utf-8').read()
    m = re.search(r'viewBox="([-\d.]+) ([-\d.]+) ([-\d.]+) ([-\d.]+)"', src)
    vx, vy, vw, vh = (float(v) for v in m.groups())
    sx, sy = w / vw, h / vh                       # dp je SVG-Einheit
    mx, my = MARGIN / sx, MARGIN / sy             # Rand in SVG-Einheiten
    ow, oh = (w + 2 * MARGIN) * K * SS, (h + 2 * MARGIN) * K * SS
    src = src.replace(m.group(0), f'viewBox="{vx - mx} {vy - my} {vw + 2 * mx} {vh + 2 * my}"', 1)
    src = re.sub(r'(<svg[^>]*?)\swidth="[^"]*"', r'\1', src, count=1)
    src = re.sub(r'(<svg[^>]*?)\sheight="[^"]*"', r'\1', src, count=1)
    png = cairosvg.svg2png(bytestring=src.encode(), output_width=round(ow), output_height=round(oh))
    big = Image.open(io.BytesIO(png)).convert('RGBA')
    X, Y = (left - MARGIN) * K, (top - MARGIN) * K  # Lage der erweiterten Box in Panel-Pixeln
    x0, y0 = math.floor(X), math.floor(Y)
    x1, y1 = math.ceil(X + ow / SS), math.ceil(Y + oh / SS)
    canvas = Image.new('RGBA', ((x1 - x0) * SS, (y1 - y0) * SS), (0, 0, 0, 0))
    canvas.alpha_composite(big, (round((X - x0) * SS), round((Y - y0) * SS)))
    im = canvas.resize((x1 - x0, y1 - y0), Image.BOX)
    bb = im.getchannel('A').getbbox() or (0, 0, 1, 1)  # leeren Rand abschneiden
    return x0 + bb[0], y0 + bb[1], im.crop(bb)

blob = bytearray()
def put(x, y, im):
    global blob
    blob += struct.pack('<hhHH', x, y, im.width, im.height) + bgra_premul(im)

def button(name, left, top, w, h, f=0xbb / 255):
    x, y, im = render(name, left, top, w, h)
    put(x, y, im); put(x, y, tint(im, f))

# Modus-Tasten (15 dp, Grafik -0,5/-0,5 dp, 16 dp): aus = eigenes Bild, an = button_led_on
for i, face in enumerate(('button_all.svg', 'button_round.svg')):
    button(face, 420 - 0.5, 14 + 26 * i - 0.5, 16, 16)
    button('button_led_on.svg', 420 - 0.5, 14 + 26 * i - 0.5, 16, 16, 0xcc / 255)
button('button_part_left.svg', 472, 14, 13, 13)
button('button_part_right.svg', 500, 14, 13, 13)
for left, top in ((538, 17), (465, 43), (538, 43), (465, 69), (538, 69), (465, 95), (538, 95)):
    button('button_left.svg', left, top, 27, 9)
    button('button_right.svg', left + 28, top, 27, 9)
knob = Image.open(A + 'knob.png').convert('RGBA')
for f in range(31):  # .volumeKnob 112/24 dp, 28 dp -> 168/36 px, 42 px
    put(168, 36, knob.crop((0, f * 128, 128, f * 128 + 128)).resize((42, 42), Image.LANCZOS))
open(out, 'wb').write(blob)
print(f"{out}: {len(blob)} Bytes")
