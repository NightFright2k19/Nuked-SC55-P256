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
import sys, math, re, os
from PIL import Image, ImageDraw, ImageFont
W, H, S, FRONT = 918, 280, 3, 196
DJ = "/usr/share/fonts/truetype/dejavu/DejaVuSans"
MONO = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
# MT-32 LCD (panel pixels): 20 cells; the GUI draws the characters (editor_mt32.inc uses the same numbers)
LX0, LY0, LX1, LY1 = 196, 100, 456, 140
CELL_W, CELL_Y, CELL_H = (LX1 - LX0 - 8) / 20.0, LY0 + 7, 26
INK_LCD = (210, 250, 110)
cm32l = '--cm32l' in sys.argv


def load_font():   # the LCD controller's character ROM (rows of 5 bits, 8 rows used), the same data the SC panels use
    here = os.path.dirname(os.path.abspath(__file__))
    t = open(os.path.join(here, '..', 'src', 'nuked-sc55', 'backend', 'lcd_font.h')).read().split('=', 1)[1]
    return [[int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]+', m)] for m in re.findall(r'\{[^{}]*\}', t)]


# dot matrix of one cell (panel pixels): 5 x 8 dots, 1.25 wide at a pitch of 1.5, 2.2 high at a pitch of 2.9
DOT_W, DOT_PX, DOT_H, DOT_PY, DOT_X0, DOT_Y0 = 1.8, 2.2, 2.3, 2.9, 1.0, 8.0
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


def key(d, x0, y0, x1, y1, face=(46, 46, 50)):
    """A raised black key: soft shadow, bevelled frame (light top left, dark bottom right), slightly domed face."""
    for k in range(5, 0, -1):    # shadow below and to the right
        d.rounded_rectangle((X(x0 - 1 + k * 0.3), X(y0 + k * 0.6), X(x1 + 1 + k * 0.3), X(y1 + 1 + k * 0.9)), X(2 + k * 0.4),
                            fill=lerp((14, 14, 16), (4, 4, 5), (6 - k) / 6))
    d.rounded_rectangle((X(x0), X(y0), X(x1), X(y1)), X(2), fill=(98, 98, 104))             # frame, light edge
    d.rounded_rectangle((X(x0 + 1), X(y0 + 1), X(x1 + 0.2), X(y1 + 0.2)), X(2), fill=(12, 12, 13))   # dark bottom / right edge
    fx0, fy0, fx1, fy1 = x0 + 1.6, y0 + 1.6, x1 - 1.6, y1 - 1.6
    h = fy1 - fy0
    for i in range(int(h * S)):    # domed face: lighter at the top, darker at the bottom
        t = i / (h * S); c = lerp((face[0] + 16, face[1] + 16, face[2] + 18), (face[0] - 22, face[1] - 22, face[2] - 22), t)
        d.line([(X(fx0) + X(1), X(fy0) + i), (X(fx1) - X(1), X(fy0) + i)], fill=c)
    d.rounded_rectangle((X(fx0), X(fy0), X(fx1), X(fy1)), X(1.5), outline=(70, 70, 76), width=X(0.8))
    d.line([(X(fx0 + 2), X(fy0 + 1.2)), (X(fx1 - 2), X(fy0 + 1.2))], fill=(104, 104, 110), width=X(0.8))   # top sheen


def build():
    im = Image.new('RGB', (W * S, H * S)); d = ImageDraw.Draw(im)
    if not cm32l: top, low, ink, lab = (44, 44, 48), (30, 30, 33), (225, 225, 228), (120, 170, 235)
    else: top, low, ink, lab = (236, 230, 204), (222, 214, 186), (60, 58, 54), (60, 58, 54)
    for y in range(FRONT * S): d.line([(0, y), (W * S, y)], fill=lerp(top, low, y / (FRONT * S)))
    d.rectangle((0, 0, W * S, X(3)), fill=lerp(top, (255, 255, 255), 0.18))
    for y in range(X(FRONT), H * S):
        d.line([(0, y), (W * S, y)], fill=lerp((58, 58, 60), (36, 36, 38), (y - X(FRONT)) / ((H - FRONT) * S)))
    d.line([(0, X(FRONT)), (W * S, X(FRONT))], fill=(20, 20, 20), width=X(2))

    def ctr(box, t, f, fill):
        l, tt, r, b = d.textbbox((0, 0), t, font=f); x0, y0, x1, y1 = box
        d.text(((X(x0) + X(x1) - (r - l)) / 2 - l, (X(y0) + X(y1) - (b - tt)) / 2 - tt), t, font=f, fill=fill)

    # VOICES module (as on the other panels; count, meter and UNITS drawn by the GUI)
    VX0, VX1 = 22, 160
    VY = -6 if cm32l else 0    # CM-32L: the whole VOICES box sits a little higher (editor_mt32.inc uses the same offset)
    d.rounded_rectangle((X(VX0 - 6), X(14 + VY), X(VX1 + 6), X(FRONT - 22 + VY)), X(5), fill=(120, 112, 96) if cm32l else (20, 20, 22))
    d.rounded_rectangle((X(VX0 - 4), X(16 + VY), X(VX1 + 4), X(FRONT - 24 + VY)), X(4), fill=(52, 52, 56))
    ctr((VX0, 18 + VY, VX1, 34 + VY), "VOICES", F(11, "-Bold"), (225, 225, 225))
    d.rectangle((X(VX0 + 2), X(37 + VY), X(VX1 - 2), X(132 + VY)), fill=(20, 20, 22))
    d.rectangle((X(VX0 + 8), X(138 + VY), X(VX1 - 8), X(156 + VY)), fill=(20, 20, 22))
    if example:
        ctr((VX0 + 2, 44 + VY, VX1 - 2, 92 + VY), "012", F(34, "-Bold"), (255, 150, 30))
        ctr((VX0 + 2, 92 + VY, VX1 - 2, 106 + VY), "of 32", F(10), (200, 200, 200))
        for i in range(32):
            x = X(VX0 + 8 + i * 3.9); d.rectangle((x, X(112 + VY), x + X(2.6), X(124 + VY)), fill=(255, 150, 30) if i < 12 else (70, 45, 20))
        ctr((VX0 + 8, 138 + VY, VX1 - 8, 156 + VY), "UNITS 1/1", F(10, "-Bold"), (255, 150, 30))
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
        lbl = F(7.5); l, tt, r_, bb = d.textbbox((0, 0), "MIDI MESSAGE", font=lbl)
        d.text((X(LX1) - (r_ - l) - l, X(LY0 - 24)), "MIDI MESSAGE", font=lbl, fill=ink)
        on = all_on
        if on:  # lamp (a small dash) with a soft glow
            for k in range(5, 0, -1):
                d.rectangle((X(LX1 - 14 - k), X(LY0 - 10 - k), X(LX1 + k), X(LY0 - 6 + k)), fill=lerp((14, 14, 16), (255, 70, 50), 0.12 * (6 - k)))
        d.rectangle((X(LX1 - 14), X(LY0 - 10), X(LX1), X(LY0 - 6)), fill=(255, 80, 60) if on else (70, 24, 24))
        # dark green LCD: a dot matrix of 20 cells of 5 x 8 dots (unlit dots faintly visible), lit dots light green
        d.rectangle((X(LX0), X(LY0), X(LX1), X(LY1)), fill=(40, 64, 14))
        txt = "1 2 3 4 5 R vol:100".ljust(20)
        font = load_font()
        for i in range(20):
            x0 = LX0 + 4 + i * CELL_W
            g = font[ord(txt[i]) - 16] if (example and txt[i] != " ") else [0] * 10
            for row in range(8):
                for col in range(5):
                    xx, yy = x0 + DOT_X0 + col * DOT_PX, LY0 + DOT_Y0 + row * DOT_PY
                    lit_dot = bool(g[row] >> (4 - col) & 1)
                    d.rectangle((X(xx), X(yy), X(xx + DOT_W), X(yy + DOT_H)), fill=INK_LCD if lit_dot else (34, 57, 12))
        if not base and not all_on:
            ov = glass_overlay(); im.paste(ov, (X(LX0), X(LY0)), ov); d = ImageDraw.Draw(im)
        # switches
        d.text((X(497), X(46)), "PART", font=F(8.5), fill=ink)
        d.line([(X(480), X(52)), (X(494), X(52))], fill=ink, width=X(1)); d.line([(X(528), X(52)), (X(580), X(52))], fill=ink, width=X(1))
        for i, l in enumerate(["1", "2", "3", "4", "5", "RHYTHM"]):
            cx, cy = 490 + (i % 3) * 44, 78 + (i // 3) * 60
            key(d, cx - 13, cy - 13, cx + 13, cy + 13)
            ctr((cx - 24, cy + 16, cx + 24, cy + 28), l, F(8), ink)
        for i, l in enumerate(["SOUND GROUP", "VOLUME", "SOUND", "MASTER VOLUME"]):
            cx, cy = 665 + (i % 2) * 92, 78 + (i // 2) * 60
            ctr((cx - 44, cy - 34, cx + 44, cy - 20), l, F(8), lab)
            key(d, cx - 30, cy - 12, cx + 30, cy + 12)
        # knob (SELECT = the device's knob with the right mouse button, GAIN = the plugin's output)
        cx, cy, r = 850, 108, 30
        ctr((cx - 60, 46, cx + 60, 62), "SELECT/GAIN", F(8.5), ink)
        for k in range(21):
            a = math.radians(-225 + k * 13.5)
            d.ellipse((X(cx + (r + 8) * math.cos(a) - 1), X(cy + (r + 8) * math.sin(a) - 1),
                       X(cx + (r + 8) * math.cos(a) + 1), X(cy + (r + 8) * math.sin(a) + 1)), fill=(150, 150, 155))
        # black knob: soft shadow, serrated side (48 grooves), domed top lit from the upper left, marker at the rim
        for k in range(6, 0, -1):
            d.ellipse((X(cx - r - 1 + k * 0.5), X(cy - r + 2 + k * 0.7), X(cx + r + 1 + k * 0.5), X(cy + r + 3 + k * 0.7)), fill=lerp((14, 14, 16), (6, 6, 7), (7 - k) / 7))
        d.ellipse((X(cx - r - 1), X(cy - r - 1), X(cx + r + 1), X(cy + r + 1)), fill=(8, 8, 9))
        n = 48
        for i in range(n):
            a0, a1 = 2 * math.pi * i / n, 2 * math.pi * (i + 0.5) / n
            lightness = 0.5 + 0.5 * math.cos((a0 + a1) / 2 + 2.35)    # the ribs facing the light are brighter
            c = lerp((20, 20, 22), (96, 96, 102), lightness)
            pts = [(cx + (r - 6) * math.cos(a0), cy + (r - 6) * math.sin(a0)), (cx + r * math.cos(a0), cy + r * math.sin(a0)),
                   (cx + r * math.cos(a1), cy + r * math.sin(a1)), (cx + (r - 6) * math.cos(a1), cy + (r - 6) * math.sin(a1))]
            d.polygon([(X(x), X(y)) for x, y in pts], fill=c)
        rt = r - 6
        for i in range(X(rt), 0, -1):    # domed top face, radial shading toward the upper left
            t = i / X(rt); off = X(rt) * 0.22 * (1 - t)
            d.ellipse((X(cx) - i - off, X(cy) - i - off, X(cx) + i - off, X(cy) + i - off), fill=lerp((78, 78, 84), (24, 24, 27), t ** 0.8))
        d.ellipse((X(cx - rt), X(cy - rt), X(cx + rt), X(cy + rt)), outline=(10, 10, 11), width=X(1.2))
        d.arc((X(cx - rt + 2), X(cy - rt + 2), X(cx + rt - 2), X(cy + rt - 2)), 200, 285, fill=(120, 120, 128), width=X(1.2))
        if example:
            a = math.radians(-90); d.line([(X(cx + 10 * math.cos(a)), X(cy + 10 * math.sin(a))), (X(cx + (r - 2) * math.cos(a)), X(cy + (r - 2) * math.sin(a)))], fill=(235, 235, 235), width=X(2.4))
            ctr((cx - 40, 150, cx + 40, 166), "+0.0 dB", F(10, "-Bold"), (255, 150, 30))
    else:
        # CM-32L: cream front, thin top bevel, logo left above the strip, a recess for the knob cut into the
        # ribbed strip, thin slit lamps and the grey slide switch on the strip, labels slanted at 60 deg
        d.rectangle((0, X(3), W * S, X(5)), fill=lerp(top, (255, 255, 255), 0.35))
        GY0, GY1 = FRONT - 31, FRONT - 13                      # ribbed strip (between the two lips)
        d.rectangle((0, X(GY0 - 1), W * S, X(GY1 + 1)), fill=(206, 198, 168))
        for x in range(0, W, 4): d.rectangle((X(x), X(GY0), X(x + 2), X(GY1)), fill=(176, 167, 138))   # wide ribs (as the first draft)
        d.line([(0, X(GY0 - 1)), (W * S, X(GY0 - 1))], fill=(170, 161, 132), width=X(1))
        d.line([(0, X(GY1 + 1)), (W * S, X(GY1 + 1))], fill=(240, 234, 210), width=X(1))
        for y in range(X(GY1 + 2), X(FRONT)): d.line([(0, y), (W * S, y)], fill=lerp((232, 225, 198), (214, 206, 176), (y - X(GY1 + 2)) / X(FRONT - GY1 - 2)))
        # logo (the plugin's own name in the device's style; no maker logo)
        gray = (92, 92, 92)
        d.text((X(196), X(104)), "Nuked", font=F(22), fill=gray)
        d.text((X(276), X(98)), "CM-32L", font=F(30, "-Bold"), fill=gray)
        d.text((X(278), X(132)), "LA SOUND MODULE", font=F(8.5), fill=gray)
        # knob in its recess
        cx, cy, r = 470, 116, 30
        tl, tr = cx - r - 8, cx + r + 8                         # the face reaches down into the strip (U-shaped notch)
        d.rounded_rectangle((X(tl), X(GY0 - 14), X(tr), X(GY1 + 1)), X(12), fill=lerp(top, low, (GY0 - 2) / FRONT), outline=(170, 161, 132), width=X(1.2))
        for y in range(X(GY0 - 14), X(GY0 - 1)): d.line([(X(tl), y), (X(tr), y)], fill=lerp(top, low, y / (FRONT * S)))
        # the surface below the knob is ribbed too, same ribs as the strip, laid out symmetrically around the knob
        RY0 = cy + r - 14
        for i in range(-12, 13):
            x = cx + 4 * i
            if tl + 3 <= x - 1 and x + 1 <= tr - 3: d.rectangle((X(x - 1), X(RY0), X(x + 1), X(GY1)), fill=(176, 167, 138))
        d.ellipse((X(cx - r + 2), X(cy - r + 5), X(cx + r + 4), X(cy + r + 7)), fill=(150, 142, 116))      # shadow
        for k in range(X(r), 0, -1):                                                                         # grey cap, lit from the top left
            t = k / X(r); d.ellipse((X(cx) - k - X(3) * (1 - t), X(cy) - k - X(3) * (1 - t), X(cx) + k - X(3) * (1 - t), X(cy) + k - X(3) * (1 - t)),
                                    fill=lerp((132, 134, 138), (92, 94, 98), t))
        d.ellipse((X(cx - r), X(cy - r), X(cx + r), X(cy + r)), outline=(70, 72, 76), width=X(1.5))
        if example:
            a = math.radians(-90); d.line([(X(cx + (r - 13) * math.cos(a)), X(cy + (r - 13) * math.sin(a))), (X(cx + (r - 3) * math.cos(a)), X(cy + (r - 3) * math.sin(a)))], fill=(215, 215, 215), width=X(2.5))

        def slanted(t, x, y, f, fill, ang=60):    # label rotated, lower left corner at (x, y)
            im2 = Image.new('RGBA', (X(120), X(20)), (0, 0, 0, 0)); ImageDraw.Draw(im2).text((0, 0), t, font=f, fill=fill + (255,))
            im2 = im2.crop(im2.getbbox()).rotate(ang, expand=True, resample=Image.BICUBIC); im.paste(im2, (X(x), X(y) - im2.size[1]), im2)
        slanted("GAIN", cx + r + 6, cy + 22, F(9), gray)
        if example: ctr((cx - 40, cy - r - 22, cx + 40, cy - r - 8), "+0.0 dB", F(9, "-Bold"), (150, 90, 10))
        for lx, t, c, dim in [(690, "MIDI MESSAGE", (40, 170, 60), (70, 96, 66)), (760, "POWER", (220, 40, 36), (120, 70, 64))]:
            on = all_on or lit
            if on:
                for k in range(4, 0, -1): d.rectangle((X(lx - 1.5 - k), X(GY0 + 3 - k), X(lx + 1.5 + k), X(GY1 - 3 + k)), fill=lerp((206, 198, 168), c, 0.10 * (5 - k)))
            d.rectangle((X(lx - 1.5), X(GY0 + 3), X(lx + 1.5), X(GY1 - 3)), fill=c if on else dim, outline=(120, 112, 90), width=X(0.6))
            slanted(t, lx - 2, GY0 - 5, F(7.5), gray)
        d.rounded_rectangle((X(780), X(169), X(816), X(180)), X(2), fill=(118, 120, 124), outline=(84, 86, 90), width=X(1))
        d.line([(X(782), X(170.5)), (X(814), X(170.5))], fill=(160, 162, 166), width=X(1))
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
