#!/usr/bin/env python3
# Draws the Nuked-MT32 panels from scratch (issue #69), modelled on photos of the MT-32 (dark front,
# green LCD behind a smoked window, PART 1-5 / RHYTHM and four function switches, knob) and of the
# CM-32L (beige front, knob, MIDI MESSAGE and POWER lamps on the grooved strip). Nothing is copied
# from the photos; the maker's logos and product names are replaced by the plugin's own branding.
# Upper 196 px = the device front; lower strip = plugin additions (VOICES, UNITS, SETUP).
# usage: make_la_panel.py <out.png|out.bgra> [--cm32l] [--lit | --all-on | --base | --glass]
#   --cm32l   the CM-32L panel (else the MT-32 panel)
#   --lit     example state (preview)                --all-on  every lamp lit (lamp sprites of the GUI)
#   --base    GUI background: lamps off, LCD without characters or glass, knob without marker, boxes empty
#   --glass   MT-32: the glass over the LCD (premultiplied BGRA of the LCD rectangle, laid over the characters)
#   .bgra: raw BGRA for gen_blob.py (918x280, --glass: the LCD rectangle), else PNG
import sys, math
from PIL import Image, ImageDraw, ImageFont
W, H, S, FRONT = 918, 280, 3, 196
DJ = "/usr/share/fonts/truetype/dejavu/DejaVuSans"
MONO = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
# MT-32 LCD (panel pixels): 20 cells; the GUI draws the characters (editor_mt32.inc uses the same numbers)
LX0, LY0, LX1, LY1 = 196, 98, 372, 138
CELL_W, CELL_Y, CELL_H = (LX1 - LX0 - 8) / 20.0, LY0 + 7, 26
INK_LCD = (210, 250, 110)
cm32l = '--cm32l' in sys.argv
lit = '--lit' in sys.argv
all_on = '--all-on' in sys.argv
base = '--base' in sys.argv
glass = '--glass' in sys.argv
example = lit                        # values / characters drawn into the picture (preview only)


def lerp(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))
def X(v): return int(round(v * S))
def F(sz, st=""): return ImageFont.truetype(DJ + st + ".ttf", int(sz * S))


def glass_overlay():
    """Glass over the LCD: soft top sheen, inner shadow at the edges, thin top highlight (RGBA, x S)."""
    gw, gh = X(LX1) - X(LX0), X(LY1) - X(LY0)
    ov = Image.new('RGBA', (gw, gh), (0, 0, 0, 0)); od = ImageDraw.Draw(ov)
    for y in range(gh):
        t = y / gh
        if t < 0.45: od.line([(0, y), (gw, y)], fill=(255, 255, 255, int(84 * (1 - t / 0.45))))
    for i in range(X(3)):
        a = int(108 * (1 - i / X(3))); od.rectangle((i, i, gw - 1 - i, gh - 1 - i), outline=(0, 0, 0, a))
    od.line([(X(2), X(1.5)), (gw - X(2), X(1.5))], fill=(255, 255, 255, 144), width=X(1))
    return ov


def build():
    im = Image.new('RGB', (W * S, H * S)); d = ImageDraw.Draw(im)
    if not cm32l: top, low, ink, lab = (44, 44, 48), (30, 30, 33), (225, 225, 228), (120, 170, 235)
    else: top, low, ink, lab = (226, 218, 190), (196, 187, 158), (60, 58, 54), (60, 58, 54)
    for y in range(FRONT * S): d.line([(0, y), (W * S, y)], fill=lerp(top, low, y / (FRONT * S)))
    d.rectangle((0, 0, W * S, X(3)), fill=lerp(top, (255, 255, 255), 0.18))
    if cm32l:   # grooved strip along the lower front
        for x in range(0, W, 4): d.rectangle((X(x), X(FRONT - 30), X(x + 2), X(FRONT - 14)), fill=(176, 167, 138))
    for y in range(X(FRONT), H * S):
        d.line([(0, y), (W * S, y)], fill=lerp((58, 58, 60), (36, 36, 38), (y - X(FRONT)) / ((H - FRONT) * S)))
    d.line([(0, X(FRONT)), (W * S, X(FRONT))], fill=(20, 20, 20), width=X(2))

    def ctr(box, t, f, fill):
        l, tt, r, b = d.textbbox((0, 0), t, font=f); x0, y0, x1, y1 = box
        d.text(((X(x0) + X(x1) - (r - l)) / 2 - l, (X(y0) + X(y1) - (b - tt)) / 2 - tt), t, font=f, fill=fill)

    # VOICES module (as on the other panels; count, meter and UNITS drawn by the GUI)
    VX0, VX1 = 22, 160
    d.rounded_rectangle((X(VX0 - 6), X(14), X(VX1 + 6), X(FRONT - 22)), X(5), fill=(120, 112, 96) if cm32l else (20, 20, 22))
    d.rounded_rectangle((X(VX0 - 4), X(16), X(VX1 + 4), X(FRONT - 24)), X(4), fill=(52, 52, 56))
    ctr((VX0, 18, VX1, 34), "VOICES", F(11, "-Bold"), (225, 225, 225))
    d.rectangle((X(VX0 + 2), X(37), X(VX1 - 2), X(132)), fill=(20, 20, 22))
    d.rectangle((X(VX0 + 8), X(138), X(VX1 - 8), X(156)), fill=(20, 20, 22))
    if example:
        ctr((VX0 + 2, 44, VX1 - 2, 92), "012", F(34, "-Bold"), (255, 150, 30))
        ctr((VX0 + 2, 92, VX1 - 2, 106), "of 32", F(10), (200, 200, 200))
        for i in range(32):
            x = X(VX0 + 8 + i * 3.9); d.rectangle((x, X(112), x + X(2.6), X(124)), fill=(255, 150, 30) if i < 12 else (70, 45, 20))
        ctr((VX0 + 8, 138, VX1 - 8, 156), "UNITS 1/1", F(10, "-Bold"), (255, 150, 30))
    # SETUP box (bottom right, as on the SC-8820 / SC-8850 panels)
    d.rectangle((X(812), X(226), X(900), X(256)), fill=(14, 14, 16))
    if example: ctr((812, 226, 900, 256), "SETUP  >", F(11, "-Bold"), (255, 150, 30))
    d.text((X(40), X(215)), "Nuked-", font=ImageFont.truetype(DJ + "-Oblique.ttf", X(30)), fill=(150, 150, 150))
    d.text((X(150), X(209)), "CM32L" if cm32l else "MT32", font=F(36, "-Bold"), fill=(170, 170, 170))
    if not cm32l:
        # display window + LCD
        d.rounded_rectangle((X(182), X(40), X(470), X(170)), X(4), fill=(14, 14, 16))
        d.text((X(196), X(50)), "Nuked", font=ImageFont.truetype(DJ + "-Oblique.ttf", X(14)), fill=(170, 170, 170))
        d.text((X(250), X(48)), "MT-32", font=F(17, "-Bold"), fill=(200, 200, 205))
        d.text((X(380), X(112)), "MIDI MESSAGE", font=F(7.5), fill=ink)
        on = all_on
        if on:  # lamp with a soft glow
            for k in range(6, 0, -1):
                d.ellipse((X(408 - 4 - k), X(128 - 4 - k), X(408 + 4 + k), X(128 + 4 + k)), fill=lerp((14, 14, 16), (255, 70, 50), 0.12 * (7 - k)))
        d.ellipse((X(404), X(124), X(412), X(132)), fill=(255, 80, 60) if on else (60, 20, 20))
        # dark green LCD: unlit cell rectangles in a grid, characters in light green
        d.rectangle((X(LX0), X(LY0), X(LX1), X(LY1)), fill=(40, 64, 14))
        txt = "1 2 3 4 5 R vol:100".ljust(20)
        fnt = ImageFont.truetype(MONO, X(13.5))
        for i in range(20):
            x0 = LX0 + 4 + i * CELL_W; y0 = CELL_Y
            d.rectangle((X(x0 + 0.7), X(y0), X(x0 + CELL_W - 0.7), X(y0 + CELL_H)), fill=(30, 50, 10))          # unlit cell
            d.rectangle((X(x0 + 0.7), X(y0 + CELL_H - 3), X(x0 + CELL_W - 0.7), X(y0 + CELL_H)), fill=(25, 42, 8))  # cursor row
            if example and txt[i] != " ":
                ctr((x0, y0 - 1, x0 + CELL_W, y0 + CELL_H - 4), txt[i], fnt, INK_LCD)
        if not base and not all_on:
            ov = glass_overlay(); im.paste(ov, (X(LX0), X(LY0)), ov); d = ImageDraw.Draw(im)
        # switches
        d.text((X(497), X(46)), "PART", font=F(8.5), fill=ink)
        d.line([(X(480), X(52)), (X(494), X(52))], fill=ink, width=X(1)); d.line([(X(528), X(52)), (X(580), X(52))], fill=ink, width=X(1))
        for i, l in enumerate(["1", "2", "3", "4", "5", "RHYTHM"]):
            cx, cy = 490 + (i % 3) * 44, 78 + (i // 3) * 60
            d.rectangle((X(cx - 13), X(cy - 13), X(cx + 13), X(cy + 13)), fill=(24, 24, 26), outline=(80, 80, 86), width=X(1.5))
            ctr((cx - 24, cy + 16, cx + 24, cy + 28), l, F(8), ink)
        for i, l in enumerate(["SOUND GROUP", "VOLUME", "SOUND", "MASTER VOLUME"]):
            cx, cy = 665 + (i % 2) * 92, 78 + (i // 2) * 60
            ctr((cx - 44, cy - 34, cx + 44, cy - 20), l, F(8), lab)
            d.rectangle((X(cx - 30), X(cy - 12), X(cx + 30), X(cy + 12)), fill=(24, 24, 26), outline=(80, 80, 86), width=X(1.5))
        # knob (SELECT = the device's knob with the right mouse button, GAIN = the plugin's output)
        cx, cy, r = 850, 108, 30
        ctr((cx - 60, 46, cx + 60, 62), "SELECT/GAIN", F(8.5), ink)
        for k in range(21):
            a = math.radians(-225 + k * 13.5)
            d.ellipse((X(cx + (r + 8) * math.cos(a) - 1), X(cy + (r + 8) * math.sin(a) - 1),
                       X(cx + (r + 8) * math.cos(a) + 1), X(cy + (r + 8) * math.sin(a) + 1)), fill=(150, 150, 155))
        d.ellipse((X(cx - r), X(cy - r), X(cx + r), X(cy + r)), fill=(22, 22, 24), outline=(70, 70, 74), width=X(2))
        d.ellipse((X(cx - r + 8), X(cy - r + 8), X(cx + r - 8), X(cy + r - 8)), fill=(34, 34, 36))
        if example:
            a = math.radians(-90); d.line([(X(cx), X(cy)), (X(cx + (r - 3) * math.cos(a)), X(cy + (r - 3) * math.sin(a)))], fill=(235, 235, 235), width=X(3))
            ctr((cx - 40, 150, cx + 40, 166), "+0.0 dB", F(10, "-Bold"), (255, 150, 30))
    else:
        d.text((X(200), X(60)), "Nuked", font=ImageFont.truetype(DJ + "-Oblique.ttf", X(26)), fill=ink)
        d.text((X(284), X(54)), "CM-32L", font=F(32, "-Bold"), fill=ink)
        cx, cy, r = 470, 116, 30
        d.ellipse((X(cx - r - 4), X(cy - r - 4), X(cx + r + 4), X(cy + r + 4)), fill=(190, 181, 152))
        d.ellipse((X(cx - r), X(cy - r), X(cx + r), X(cy + r)), fill=(110, 110, 112), outline=(80, 80, 82), width=X(2))
        if example:
            a = math.radians(-90); d.line([(X(cx), X(cy)), (X(cx + (r - 4) * math.cos(a)), X(cy + (r - 4) * math.sin(a)))], fill=(235, 235, 235), width=X(3))

        def slanted(t, x, y, f, fill):    # label rotated 35 deg, lower left corner at (x, y)
            im2 = Image.new('RGBA', (X(120), X(20)), (0, 0, 0, 0)); ImageDraw.Draw(im2).text((0, 0), t, font=f, fill=fill + (255,))
            im2 = im2.crop(im2.getbbox()).rotate(35, expand=True); im.paste(im2, (X(x), X(y) - im2.size[1]), im2)
        slanted("GAIN", cx + r + 4, cy + 4, F(10, "-Bold"), ink)
        if example: ctr((cx - 40, cy + r + 8, cx + 40, cy + r + 22), "+0.0 dB", F(9, "-Bold"), (150, 90, 10))
        for lx, t, c, dim in [(690, "MIDI MESSAGE", (30, 150, 40), (40, 70, 40)), (760, "POWER", (190, 30, 30), (80, 40, 38))]:
            on = all_on or lit
            d.rectangle((X(lx - 7), X(168), X(lx + 7), X(180)), fill=c if on else dim, outline=(90, 90, 92), width=X(1))
            slanted(t, lx - 4, 164, F(8, "-Bold"), ink)
        d.rectangle((X(784), X(168), X(816), X(180)), fill=(120, 120, 122), outline=(90, 90, 92), width=X(1))
    return im.resize((W, H), Image.LANCZOS)


out = sys.argv[1]
if glass:
    ov = glass_overlay().resize((LX1 - LX0, LY1 - LY0), Image.LANCZOS)
    if out.endswith('.bgra'):
        px = bytearray()
        for (r, g, b, a) in zip(*[iter(ov.tobytes())] * 4):   # premultiplied for AlphaBlend
            px += bytes((b * a // 255, g * a // 255, r * a // 255, a))
        open(out, 'wb').write(px)
    else:
        ov.save(out)
else:
    im = build()
    if out.endswith('.bgra'):
        open(out, 'wb').write(im.convert('RGBA').tobytes('raw', 'BGRA'))
    else:
        im.save(out)
