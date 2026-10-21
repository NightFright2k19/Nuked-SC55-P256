#!/usr/bin/env python3
# Creates the controls of the SC-88 panel from the original graphics of the 88emu player
# (Gearmulator, The Usual Suspects, GPLv3): buttons, rockers and LEDs from the SVGs, GAIN knob from
# knob.png (31 positions). Position and size as in the player skin (emu88Player.rml/.rcss, dp),
# panel = dp x 1.5 (918x280). Each graphic is rendered at its own (sub-pixel) position
# (8x oversampled), with a margin for shadow/glow outside the SVG box.
# Pressed: player tint image-color #bbb (LED image #ccc).
# Output: per graphic int16 x, int16 y, uint16 w, uint16 h (little endian), then w*h
# premultiplied BGRA pixels (for AlphaBlend), in a fixed order:
#   0..15   ALL, MUTE, INST MAP (SC-55 MAP), EQ: each [off, off pressed, on, on pressed]
#   16..19  PART <, PART >: each [normal, pressed]
#   20..21  PREVIEW [normal, pressed]
#   22..25  USER INST, SELECT: each [normal, pressed]
#   26..65  10 rockers (INSTRUMENT, LEVEL, PAN, REVERB, CHORUS, KEY SHIFT, MIDI CH, bottom 1..3):
#           each left [normal, pressed], right [normal, pressed]
#   66..71  selection LEDs (top, middle, bottom): each [off, on]
#   72..73  USER INST EDIT LED [off, on]
#   74..104 GAIN 31 positions
#   105..106 EFX LED of the SC-88 Pro green, yellow (72/73 = off / red)
# usage: make_sc88orig_sprites.py <88emuplayer/assets> <out.bgra>
import io, math, re, struct, sys
import cairosvg
from PIL import Image
A, out = sys.argv[1].rstrip('/') + '/', sys.argv[2]
SS, K, MARGIN = 8, 1.5, 4.0  # oversampling, px per dp, margin in dp

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
    """SVG (box w x h dp at left/top dp, stretched) including margin -> (x0, y0, image) in panel pixels."""
    src = open(A + name, encoding='utf-8').read()
    m = re.search(r'viewBox="([-\d.]+) ([-\d.]+) ([-\d.]+) ([-\d.]+)"', src)
    vx, vy, vw, vh = (float(v) for v in m.groups())
    sx, sy = w / vw, h / vh                       # dp per SVG unit
    mx, my = MARGIN / sx, MARGIN / sy             # margin in SVG units
    ow, oh = (w + 2 * MARGIN) * K * SS, (h + 2 * MARGIN) * K * SS
    src = src.replace(m.group(0), f'viewBox="{vx - mx} {vy - my} {vw + 2 * mx} {vh + 2 * my}"', 1)
    src = re.sub(r'(<svg[^>]*?)\swidth="[^"]*"', r'\1', src, count=1)
    src = re.sub(r'(<svg[^>]*?)\sheight="[^"]*"', r'\1', src, count=1)
    png = cairosvg.svg2png(bytestring=src.encode(), output_width=round(ow), output_height=round(oh))
    big = Image.open(io.BytesIO(png)).convert('RGBA')
    X, Y = (left - MARGIN) * K, (top - MARGIN) * K  # position of the extended box in panel pixels
    x0, y0 = math.floor(X), math.floor(Y)
    x1, y1 = math.ceil(X + ow / SS), math.ceil(Y + oh / SS)
    canvas = Image.new('RGBA', ((x1 - x0) * SS, (y1 - y0) * SS), (0, 0, 0, 0))
    canvas.alpha_composite(big, (round((X - x0) * SS), round((Y - y0) * SS)))
    im = canvas.resize((x1 - x0, y1 - y0), Image.BOX)
    bb = im.getchannel('A').getbbox() or (0, 0, 1, 1)  # cut off the empty margin
    return x0 + bb[0], y0 + bb[1], im.crop(bb)

blob = bytearray()
def put(x, y, im):
    global blob
    blob += struct.pack('<hhHH', x, y, im.width, im.height) + bgra_premul(im)

def button(name, left, top, w, h, f=0xbb / 255):
    x, y, im = render(name, left, top, w, h)
    put(x, y, im); put(x, y, tint(im, f))

# mode buttons (15 dp, graphic -0.5/-0.5 dp, 16 dp): off = own image, on = button_led_on
for i, face in enumerate(('button_all.svg', 'button_round.svg', 'button_round.svg', 'button_round.svg')):
    button(face, 420 - 0.5, 14 + 26 * i - 0.5, 16, 16)
    button('button_led_on.svg', 420 - 0.5, 14 + 26 * i - 0.5, 16, 16, 0xcc / 255)
button('button_part_left.svg', 472, 14, 13, 13)
button('button_part_right.svg', 500, 14, 13, 13)
button('preview.svg', 116, 78, 20, 20)
button('button_plain.svg', 311, 166, 27, 9)
button('button_plain.svg', 339, 166, 27, 9)
for left, top in ((538, 17), (465, 43), (538, 43), (465, 69), (538, 69), (465, 95), (538, 95),
                  (392, 166), (465, 166), (538, 166)):
    button('button_left.svg', left, top, 27, 9)
    button('button_right.svg', left + 28, top, 27, 9)
for top in (132, 143, 154):
    for face in ('selector_led_off.svg', 'selector_led_on.svg'):
        put(*render(face, 378.25, top, 8.3467, 8))
for face in ('efx_led_off.svg', 'efx_led_on.svg'):
    put(*render(face, 287.5, 165.5, 9, 9))
knob = Image.open(A + 'knob.png').convert('RGBA')
for f in range(31):  # .volumeKnob 112/24 dp, 28 dp -> 168/36 px, 42 px
    put(168, 36, knob.crop((0, f * 128, 128, f * 128 + 128)).resize((42, 42), Image.LANCZOS))
for face in ('efx_led_green.svg', 'efx_led_amber.svg'):
    put(*render(face, 287.5, 165.5, 9, 9))
open(out, 'wb').write(blob)
print(f"{out}: {len(blob)} Bytes")
