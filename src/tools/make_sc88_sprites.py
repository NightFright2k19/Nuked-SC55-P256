#!/usr/bin/env python3
# Erzeugt die Bedienelemente des SC-88-Pro-Panels aus den Original-Grafiken des 88emu-Players
# (Gearmulator, The Usual Suspects, GPLv3): Tasten aus den SVGs (4x gerendert, dann verkleinert),
# VOLUME-Regler aus knob.png (31 Stellungen). Groessen = Player-Masse (dp) x 1,5 (Panel 918x280).
# Ausgabe: ein Block vormultiplizierter BGRA-Pixel in fester Reihenfolge (fuer AlphaBlend):
#   ALL 24, ROUND 24, LED_ON 24, PART_L 20, PART_R 20, PREVIEW 30, KNOB 42x42 x 31
# usage: make_sc88_sprites.py <88emuplayer/assets> <out.bgra>
import io, sys
import cairosvg
from PIL import Image
A, out = sys.argv[1].rstrip('/') + '/', sys.argv[2]
def svg(name, size):
    png = cairosvg.svg2png(url=A + name, output_width=size * 4, output_height=size * 4)
    return Image.open(io.BytesIO(png)).convert('RGBA').resize((size, size), Image.LANCZOS)
def bgra_premul(im):
    d = im.tobytes(); b = bytearray(len(d))
    for i in range(0, len(d), 4):
        r, g, bl, a = d[i], d[i + 1], d[i + 2], d[i + 3]
        b[i:i + 4] = bytes((bl * a // 255, g * a // 255, r * a // 255, a))
    return b
blob = bytearray()
for name, size in (("button_all.svg", 24), ("button_round.svg", 24), ("button_led_on.svg", 24),
                   ("button_part_left.svg", 20), ("button_part_right.svg", 20), ("preview.svg", 30)):
    blob += bgra_premul(svg(name, size))
knob = Image.open(A + 'knob.png').convert('RGBA')
for f in range(31):
    blob += bgra_premul(knob.crop((0, f * 128, 128, f * 128 + 128)).resize((42, 42), Image.LANCZOS))
open(out, 'wb').write(blob)
print(f"{out}: {len(blob)} Bytes")
