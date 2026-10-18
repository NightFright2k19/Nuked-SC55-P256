#!/usr/bin/env python3
# Draws the Emu-MT32 panels from scratch (issue #69), modelled on photos of the MT-32 (dark front,
# green LCD behind a smoked window, PART 1-5 / RHYTHM and four function switches, knob) and of the
# CM-32L (beige front, knob, MIDI MESSAGE and POWER lamps on the grooved strip). Nothing is copied
# from the photos; the maker's logos and product names are replaced by the plugin's own branding.
# Upper 196 px = the device front; lower strip = plugin additions (VOICES, UNITS, SETUP).
# usage: make_la_panel.py <out.png|out.bgra> [--cm32l | --v2] [--lit | --all-on | --base | --glass]
#   --cm32l   the CM-32L panel (else the MT-32 panel)
#   --v2      the MT-32 panel of the new MT-32 (v2.x ROMs): "v2" after the name (issue #79)
#   --lit     example state (preview)                --all-on  every lamp lit (lamp sprites of the GUI)
#   --base    GUI background: lamps off, LCD without characters or glass, knob without marker, boxes empty
#   --glass   MT-32: the glass over the LCD (premultiplied BGRA of the LCD rectangle, laid over the characters)
#   .bgra: raw BGRA for gen_blob.py (918x280, --glass: the LCD rectangle), else PNG
import sys, math, re, os
from PIL import Image, ImageDraw, ImageFont
W, H, S, FRONT = 918, 280, 3, 196
VSCALE = 0.9                     # VOICES box scale (issue #79; editor_mt32.inc scales its contents the same way)
LOGO_BASE = 257                  # baseline of the name in the lower strip (editor_mt32.inc: click area above it)
DJ = "/usr/share/fonts/truetype/dejavu/DejaVuSans"
MONO = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
# MT-32 LCD (panel pixels): 20 cells; the GUI draws the characters (editor_mt32.inc uses the same numbers)
LX0, LY0, LX1, LY1 = 190, 100, 418, 140
CELL_X0, CELL_W = 4, 11          # 20 cells at whole pixels (issue #79)
INK_LCD = (236, 254, 8)        # lit dots: bright yellow green, as on the device (issue #79)
UNLIT_LCD = (166, 206, 16)     # unlit dots, clearly visible on the green background
cm32l = '--cm32l' in sys.argv
v2 = '--v2' in sys.argv and not cm32l


def load_font():   # the LCD controller's character ROM (rows of 5 bits, 8 rows used), the same data the SC panels use
    here = os.path.dirname(os.path.abspath(__file__))
    t = open(os.path.join(here, '..', 'src', 'nuked-sc55', 'backend', 'lcd_font.h')).read().split('=', 1)[1]
    return [[int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]+', m)] for m in re.findall(r'\{[^{}]*\}', t)]


# dot matrix of one cell, set at whole panel pixels (issue #79: square dots like the MT-32's LCD, no beating
# against the pixel grid): 5 x 8 dots of one pixel at a pitch of 2 from (cell x, LY0 + DOT_Y0); the pixel between
# two lit dots is lit too (a little darker), so lit characters read as solid dot blocks; between unlit dots the cell
# ground shows, so empty cells read as dotted rectangles. The cursor line (never lit) lies 3 pixels below the 8th row.
DOT_Y0, ULINE_DY = 12, 17
LCD_TOP, LCD_BOT = (94, 164, 3), (62, 144, 1)        # LCD ground, top and bottom (vertical gradient)
VARY = [1.0, 0.97, 1.02, 0.98, 1.0, 1.03, 0.99, 0.96, 1.01, 1.0, 1.02, 0.98, 1.0, 0.99, 1.03, 0.97, 1.0, 0.9, 1.01, 0.99]


def lcd_ground(y): return lerp(LCD_TOP, LCD_BOT, (y - LY0) / (LY1 - LY0))


def lcd_dots(im, txt):
    """20 cells of 5 x 8 dots at panel pixels (see above); slight variations in brightness from cell to cell."""
    px = im.load(); font = load_font()
    for i in range(20):
        x0, y0 = LX0 + CELL_X0 + i * CELL_W, LY0 + DOT_Y0
        g = font[ord(txt[i]) - 16] if txt[i] != " " else [0] * 10
        on = lambda c, r: 0 <= c < 5 and 0 <= r < 8 and bool(g[r] >> (4 - c) & 1)
        un = tuple(min(255, int(c * VARY[i])) for c in UNLIT_LCD)
        for yy in range(15):
            for xx in range(9):
                gr = lerp(lcd_ground(y0 + yy), un, 0.3)          # cell ground
                c, r = xx // 2, yy // 2
                if xx % 2 == 0 and yy % 2 == 0: col = INK_LCD if on(c, r) else un
                else:    # gap pixel: lit between lit dots, a little glow next to a single lit dot, else the cell ground
                    nb = [(c, r), (c + 1, r)] if yy % 2 == 0 else [(c, r), (c, r + 1)] if xx % 2 == 0 else \
                         [(c, r), (c + 1, r), (c, r + 1), (c + 1, r + 1)]
                    k = sum(on(*q) for q in nb)
                    col = INK_LCD if k else gr    # bold: a lit dot also lights its gaps (issue #79, readable at 918 px)
                px[x0 + xx, y0 + yy] = col
        for xx in range(9):    # cursor line
            px[x0 + xx, y0 + ULINE_DY] = lerp(un, lcd_ground(y0 + ULINE_DY), 0.3 if xx % 2 == 0 else 0.6)
lit = '--lit' in sys.argv
all_on = '--all-on' in sys.argv
base = '--base' in sys.argv
glass = '--glass' in sys.argv
example = lit                        # values / characters drawn into the picture (preview only)


def lerp(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))
def X(v): return int(round(v * S))
def F(sz, st=""): return ImageFont.truetype(DJ + st + ".ttf", int(sz * S))


def glass_overlay():
    """Glass over the LCD: faint top sheen, two soft diagonal reflections, a light inner shadow at the edges (RGBA, x S)."""
    gw, gh = X(LX1) - X(LX0), X(LY1) - X(LY0)
    ov = Image.new('RGBA', (gw, gh), (0, 0, 0, 0)); od = ImageDraw.Draw(ov)
    for y in range(gh):
        t = y / gh
        if t < 0.35: od.line([(0, y), (gw, y)], fill=(255, 255, 255, int(34 * (1 - t / 0.35))))
    for x0, w, a in [(0.18, 0.07, 22), (0.29, 0.025, 16), (0.71, 0.05, 12)]:    # diagonal reflections
        band = Image.new('RGBA', (gw, gh), (0, 0, 0, 0)); bd = ImageDraw.Draw(band)
        for k in range(int(w * gw)):
            al = int(a * math.sin(math.pi * k / (w * gw)))
            bd.line([(x0 * gw + k + gh * 0.6, 0), (x0 * gw + k, gh)], fill=(255, 255, 255, al), width=1)
        ov = Image.alpha_composite(ov, band); od = ImageDraw.Draw(ov)
    for i in range(X(2)):
        a = int(70 * (1 - i / X(2))); od.rounded_rectangle((i, i, gw - 1 - i, gh - 1 - i), X(4), outline=(0, 0, 0, a))
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


# squared lettering of the device's model name (CM-32L, LA SOUND MODULE): strokes on a 4 x 5 grid, corners rounded
SQ = {"C": [[(4, 0), (0, 0), (0, 5), (4, 5)]], "M": [[(0, 5), (0, 0), (4, 0), (4, 5)], [(2, 0), (2, 5)]],
      "-": [[(0.4, 2.5), (3.2, 2.5)]], "3": [[(0, 0), (4, 0), (4, 5), (0, 5)], [(1.2, 2.5), (4, 2.5)]],
      "2": [[(0, 0), (4, 0), (4, 2.5), (0, 2.5), (0, 5), (4, 5)]], "L": [[(0, 0), (0, 5), (4, 5)]],
      "A": [[(0, 5), (0, 0), (4, 0), (4, 5)], [(0, 2.6), (4, 2.6)]], "S": [[(4, 0), (0, 0), (0, 2.5), (4, 2.5), (4, 5), (0, 5)]],
      "O": [[(2, 0), (4, 0), (4, 5), (0, 5), (0, 0), (2, 0)]], "U": [[(0, 0), (0, 5), (4, 5), (4, 0)]],
      "N": [[(0, 5), (0, 0), (4, 0), (4, 5)]], "D": [[(2, 0), (3, 0), (4, 1), (4, 4), (3, 5), (0, 5), (0, 0), (2, 0)]],
      "E": [[(4, 0), (0, 0), (0, 5), (4, 5)], [(0, 2.5), (3.2, 2.5)]], " ": []}


def squared(d, x, y, text, h, sw, fill, width=None):
    """Squared lettering, top left (x, y), cap height h, stroke sw (panel pixels); width: stretch the spacing to it.
    Returns the width."""
    u = (h - sw) / 5.0
    adv = [(2.6 if c == " " else 3.6 if c == "-" else 4.0) * u + sw + 0.9 * u for c in text]
    total = sum(adv) - 0.9 * u
    gap = (width - total) / max(1, len(text) - 1) if width else 0
    cx = x
    for c, a in zip(text, adv):
        for pl in SQ[c]:
            pts = [(cx + sw / 2 + px * u, y + sw / 2 + py * u) for px, py in pl]
            rc = min(1.2 * u, sw)                          # rounded corners
            out = [pts[0]]
            for i in range(1, len(pts) - 1):
                (ax, ay), (bx, by), (qx, qy) = pts[i - 1], pts[i], pts[i + 1]
                la, lb = math.hypot(bx - ax, by - ay), math.hypot(qx - bx, qy - by)
                p0 = (bx - (bx - ax) / la * min(rc, la / 2), by - (by - ay) / la * min(rc, la / 2))
                p1 = (bx + (qx - bx) / lb * min(rc, lb / 2), by + (qy - by) / lb * min(rc, lb / 2))
                for k in range(9):
                    t = k / 8
                    out.append(((1 - t) ** 2 * p0[0] + 2 * (1 - t) * t * bx + t * t * p1[0], (1 - t) ** 2 * p0[1] + 2 * (1 - t) * t * by + t * t * p1[1]))
            out.append(pts[-1])
            d.line([(X(px), X(py)) for px, py in out], fill=fill, width=X(sw), joint="curve")
            for px, py in (out[0], out[-1]):
                d.ellipse((X(px - sw / 2), X(py - sw / 2), X(px + sw / 2), X(py + sw / 2)), fill=fill)
        cx += a + gap
    return total + gap * (len(text) - 1)


def age(im, box):
    """Aged plastic: slight yellowing in soft patches and a fine grain over box (x S)."""
    import random
    rnd = random.Random(32)
    x0, y0, x1, y1 = box; w, h = x1 - x0, y1 - y0
    patches = Image.new('L', (24, 6)); patches.putdata([rnd.randint(0, 255) for _ in range(24 * 6)])
    patches = patches.resize((w, h), Image.BICUBIC)
    yellow = Image.new('RGB', (w, h), (214, 196, 140))
    reg = im.crop(box)
    reg = Image.composite(Image.blend(reg, yellow, 0.16), reg, patches)
    grain = Image.effect_noise((w, h), 10).point(lambda v: 128 + (v - 128) // 3)
    reg = Image.blend(reg, Image.merge('RGB', (grain, grain, grain)), 0.035)
    im.paste(reg, box)


def build():
    im = Image.new('RGB', (W * S, H * S)); d = ImageDraw.Draw(im)
    if not cm32l: top, low, ink, lab = (44, 44, 48), (30, 30, 33), (225, 225, 228), (120, 170, 235)
    else: top, low, ink, lab = (232, 225, 194), (217, 208, 174), (60, 58, 54), (60, 58, 54)
    for y in range(FRONT * S): d.line([(0, y), (W * S, y)], fill=lerp(top, low, y / (FRONT * S)))
    d.rectangle((0, 0, W * S, X(3)), fill=lerp(top, (255, 255, 255), 0.18))
    for y in range(X(FRONT), H * S):
        d.line([(0, y), (W * S, y)], fill=lerp((58, 58, 60), (36, 36, 38), (y - X(FRONT)) / ((H - FRONT) * S)))
    d.line([(0, X(FRONT)), (W * S, X(FRONT))], fill=(20, 20, 20), width=X(2))
    if cm32l: age(im, (0, 0, W * S, X(FRONT)))

    def ctr(box, t, f, fill):
        l, tt, r, b = d.textbbox((0, 0), t, font=f); x0, y0, x1, y1 = box
        d.text(((X(x0) + X(x1) - (r - l)) / 2 - l, (X(y0) + X(y1) - (b - tt)) / 2 - tt), t, font=f, fill=fill)

    # VOICES module (as on the other panels; count, meter and UNITS drawn by the GUI)
    # VOICES module, 90 % of its first size (issue #79), drawn on a layer and scaled about its centre
    full, vl = im, Image.new('RGBA', im.size, (0, 0, 0, 0)); vd = ImageDraw.Draw(vl)

    def vctr(box, t, f, fill):
        l, tt, r, b = vd.textbbox((0, 0), t, font=f); x0, y0, x1, y1 = box
        vd.text(((X(x0) + X(x1) - (r - l)) / 2 - l, (X(y0) + X(y1) - (b - tt)) / 2 - tt), t, font=f, fill=fill)
    VX0, VX1 = 22, 160
    VY = -6 if cm32l else 0    # CM-32L: the whole VOICES box sits a little higher (editor_mt32.inc uses the same offset)
    vd.rounded_rectangle((X(VX0 - 6), X(14 + VY), X(VX1 + 6), X(FRONT - 22 + VY)), X(5), fill=(120, 112, 96) if cm32l else (20, 20, 22))
    vd.rounded_rectangle((X(VX0 - 4), X(16 + VY), X(VX1 + 4), X(FRONT - 24 + VY)), X(4), fill=(52, 52, 56))
    vctr((VX0, 18 + VY, VX1, 34 + VY), "VOICES", F(11, "-Bold"), (225, 225, 225))
    vd.rectangle((X(VX0 + 2), X(37 + VY), X(VX1 - 2), X(132 + VY)), fill=(20, 20, 22))
    vd.rectangle((X(VX0 + 8), X(138 + VY), X(VX1 - 8), X(156 + VY)), fill=(20, 20, 22))
    if example:
        vctr((VX0 + 2, 44 + VY, VX1 - 2, 92 + VY), "012", F(34, "-Bold"), (255, 150, 30))
        vctr((VX0 + 2, 92 + VY, VX1 - 2, 106 + VY), "of 32", F(10), (200, 200, 200))
        for i in range(32):
            x = X(VX0 + 8 + i * 3.9); vd.rectangle((x, X(112 + VY), x + X(2.6), X(124 + VY)), fill=(255, 150, 30) if i < 12 else (70, 45, 20))
        vctr((VX0 + 8, 138 + VY, VX1 - 8, 156 + VY), "UNITS 1/1", F(10, "-Bold"), (255, 150, 30))
    VB = (X(VX0 - 6), X(14 + VY), X(VX1 + 6), X(FRONT - 22 + VY)); vcx, vcy = (VB[0] + VB[2]) / 2, (VB[1] + VB[3]) / 2
    part = vl.crop(VB); part = part.resize((int(part.size[0] * VSCALE), int(part.size[1] * VSCALE)), Image.LANCZOS)
    im.paste(part, (int(vcx - part.size[0] / 2), int(vcy - part.size[1] / 2)), part)
    # SETUP box (bottom right, as on the SC-8820 / SC-8850 panels)
    d.rectangle((X(812), X(226), X(900), X(256)), fill=(14, 14, 16))
    if example: ctr((812, 226, 900, 256), "SETUP  >", F(11, "-Bold"), (255, 150, 30))
    # name (bottom left; issue #79: lower, its letters stand on y = LOGO_BASE; a click switches the device)
    x = X(40)
    for t, f, c in [("Emu-", ImageFont.truetype(DJ + "-Oblique.ttf", X(30)), (150, 150, 150)),
                    ("CM32L" if cm32l else "MT32", F(36, "-Bold"), (170, 170, 170))] + ([(" v2", F(30, "-Bold"), (255, 150, 30))] if v2 else []):
        d.text((x, X(LOGO_BASE) - d.textbbox((0, 0), "E", font=f)[3]), t, font=f, fill=c)
        x += d.textlength(t, font=f)
    if not cm32l:
        # display window + LCD
        d.rounded_rectangle((X(182), X(40), X(470), X(170)), X(4), fill=(14, 14, 16))
        d.text((X(196), X(50)), "Emu", font=ImageFont.truetype(DJ + "-Oblique.ttf", X(14)), fill=(170, 170, 170))
        d.text((X(196) + d.textlength("Emu  ", font=ImageFont.truetype(DJ + "-Oblique.ttf", X(14))), X(48)), "MT-32", font=F(17, "-Bold"), fill=(200, 200, 205))
        # MIDI MESSAGE: label and green rectangular lamp right of the LCD (issue #79)
        lbl = F(7.5)
        ctr((LX1 + 2, LY0 + 0, 470, LY0 + 10), "MIDI", lbl, ink); ctr((LX1 + 2, LY0 + 10, 470, LY0 + 20), "MESSAGE", lbl, ink)
        on = all_on or lit
        mx0, mx1, my0, my1 = 436, 452, LY0 + 25, LY0 + 31
        if on:  # lamp with a soft glow
            for k in range(5, 0, -1):
                d.rectangle((X(mx0 - k), X(my0 - k), X(mx1 + k), X(my1 + k)), fill=lerp((14, 14, 16), (40, 255, 60), 0.12 * (6 - k)))
        d.rectangle((X(mx0), X(my0), X(mx1), X(my1)), fill=(60, 255, 70) if on else (18, 60, 22))
        # LCD (issue #79, after the MT-32): long narrow rounded rectangle, vivid green, saturated green edge;
        # 20 cells of 5 x 8 square dots (unlit dots clearly visible, lit dots bright yellow green), a cursor line
        # of 5 dots under each cell; slight variations in brightness from cell to cell
        d.rounded_rectangle((X(LX0 - 1), X(LY0 - 1), X(LX1 + 1), X(LY1 + 1)), X(5), fill=(30, 104, 0))
        lcd = Image.new('RGB', (X(LX1) - X(LX0), X(LY1) - X(LY0))); ld = ImageDraw.Draw(lcd)
        for y in range(lcd.size[1]): ld.line([(0, y), (lcd.size[0], y)], fill=lerp((94, 164, 3), (62, 144, 1), y / lcd.size[1]))
        mask = Image.new('L', lcd.size, 0); ImageDraw.Draw(mask).rounded_rectangle((0, 0, lcd.size[0] - 1, lcd.size[1] - 1), X(4), fill=255)
        im.paste(lcd, (X(LX0), X(LY0)), mask)
        # (the dots are set at panel pixels after the downscale: lcd_dots)
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
        # CM-32L (issue #79, after the photo): aged ivory front, thin top bevel, logo upper left in the device's
        # lettering ("Emu" in place of the maker's name), a dark knob in a U-shaped recess cut into the ribbed strip,
        # "VOLUME", the MIDI MESSAGE and POWER slit lamps and the grey power switch on the strip, labels at 45 deg
        d.rectangle((0, X(3), W * S, X(5)), fill=lerp(top, (255, 255, 255), 0.35))
        GY0, GY1 = FRONT - 31, FRONT - 13                      # ribbed strip (between the two lips)
        RIB_G, RIB_L, RIB_D = (168, 158, 128), (234, 226, 198), (196, 187, 156)    # gap, rib crest, rib flank

        def ribs(x0, x1, y0, y1, xs):                          # rounded ribs: light crest, darker flanks, dark gaps
            d.rectangle((X(x0), X(y0), X(x1), X(y1)), fill=RIB_G)
            for x in xs:
                for k in range(X(2.8)):
                    t = abs(k / X(2.8) - 0.4) / 0.6
                    d.line([(X(x) + k, X(y0)), (X(x) + k, X(y1))], fill=lerp(RIB_L, RIB_D, t))
        ribs(0, W, GY0, GY1, range(0, W, 4))
        d.line([(0, X(GY0 - 1)), (W * S, X(GY0 - 1))], fill=(150, 141, 112), width=X(1.4))    # shadow under the upper lip
        d.line([(0, X(GY1 + 1)), (W * S, X(GY1 + 1))], fill=(244, 238, 216), width=X(1))
        for y in range(X(GY1 + 2), X(FRONT)): d.line([(0, y), (W * S, y)], fill=lerp((232, 225, 198), (212, 204, 172), (y - X(GY1 + 2)) / X(FRONT - GY1 - 2)))
        # logo: "Emu" where the maker's name is, "CM-32L" and "LA SOUND MODULE" in the device's squared lettering
        gray = (74, 80, 82)
        fe = F(25); d.text((X(192), X(100) - d.textbbox((0, 0), "E", font=fe)[1]), "Emu", font=fe, fill=gray, stroke_width=X(0.3), stroke_fill=gray)
        w = squared(d, 266, 100, "CM-32L", 20, 3.3, gray)
        squared(d, 267, 126, "LA SOUND MODULE", 6.2, 1.15, gray, width=w - 2)
        # knob in its recess
        cx, cy, r = 470, 116, 30
        tl, tr = cx - r - 8, cx + r + 8                         # the face reaches down into the strip (U-shaped notch)
        # below the knob (issue #79 mockup): a ribbed column as wide as the knob, from its centre down into the strip
        ribs(cx - r, cx + r, cy, GY1, [x for x in range(cx - 48, cx + 48, 4) if cx - r <= x and x + 3 <= cx + r])
        sh = Image.new('RGBA', im.size, (0, 0, 0, 0)); sd = ImageDraw.Draw(sh)                              # soft shadow, the ribs show through
        for k in range(8, 0, -1):
            sd.ellipse((X(cx - r + 1 + k * 0.4), X(cy - r + 3 + k * 0.6), X(cx + r + 3 + k * 0.4), X(cy + r + 5 + k * 0.6)), fill=(50, 40, 22, 34))
        im.paste(Image.alpha_composite(im.convert('RGBA'), sh).convert('RGB')); d = ImageDraw.Draw(im)
        d.ellipse((X(cx - r), X(cy - r + 3), X(cx + r), X(cy + r + 3)), fill=(46, 48, 50))                   # skirt (cylinder side)
        for k in range(X(r), 0, -1):                                                                         # charcoal cap, lit from the top left
            t = k / X(r); o = X(3) * (1 - t)
            d.ellipse((X(cx) - k - o, X(cy) - k - o, X(cx) + k - o, X(cy) + k - o), fill=lerp((126, 129, 130), (84, 87, 89), t ** 1.4))
        d.ellipse((X(cx - r), X(cy - r), X(cx + r), X(cy + r)), outline=(44, 46, 48), width=X(1.2))
        d.arc((X(cx - r + 1.5), X(cy - r + 1.5), X(cx + r - 1.5), X(cy + r - 1.5)), 190, 280, fill=(150, 153, 154), width=X(1))
        if example:    # engraved marker: dark groove with a light lower edge
            a = math.radians(-90)
            p0, p1 = (cx + (r - 13) * math.cos(a), cy + (r - 13) * math.sin(a)), (cx + (r - 3) * math.cos(a), cy + (r - 3) * math.sin(a))
            d.line([(X(p0[0] + 0.6), X(p0[1] + 0.6)), (X(p1[0] + 0.6), X(p1[1] + 0.6))], fill=(150, 152, 152), width=X(2.2))
            d.line([(X(p0[0]), X(p0[1])), (X(p1[0]), X(p1[1]))], fill=(40, 42, 44), width=X(2))

        def slanted(t, x, y, f, fill, ang=45):    # label rotated, lower left corner at (x, y)
            im2 = Image.new('RGBA', (X(120), X(20)), (0, 0, 0, 0)); ImageDraw.Draw(im2).text((0, 0), t, font=f, fill=fill + (255,))
            im2 = im2.crop(im2.getbbox()).rotate(ang, expand=True, resample=Image.BICUBIC); im.paste(im2, (X(x), X(y) - im2.size[1]), im2)
        slanted("GAIN", cx + r + 4, cy + 26, F(8), gray)
        if example: ctr((cx - 40, cy - r - 22, cx + 40, cy - r - 8), "+0.0 dB", F(9, "-Bold"), (150, 90, 10))
        for lx, t, c, dim in [(690, "MIDI MESSAGE", (60, 200, 90), (52, 92, 72)), (760, "POWER", (236, 64, 84), (122, 52, 64))]:
            on = all_on or lit
            if on:
                for k in range(4, 0, -1): d.rectangle((X(lx - 1.5 - k), X(GY0 + 3 - k), X(lx + 1.5 + k), X(GY1 - 3 + k)), fill=lerp(RIB_D, c, 0.10 * (5 - k)))
            d.rectangle((X(lx - 2), X(GY0 + 2), X(lx + 2), X(GY1 - 2)), fill=c if on else dim, outline=(110, 102, 80), width=X(0.6))
            slanted(t, lx - 3, GY0 - 4, F(7.5), gray)
        # power switch: dark grey block standing out of the strip, shadow below and to the right
        d.rectangle((X(781), X(GY0 + 3), X(830), X(GY1 + 1)), fill=(120, 112, 88))
        d.rounded_rectangle((X(778), X(GY0 + 1), X(826), X(GY1 - 1)), X(1.2), fill=(108, 110, 108), outline=(70, 72, 72), width=X(0.8))
        d.line([(X(779), X(GY0 + 2)), (X(825), X(GY0 + 2))], fill=(150, 152, 150), width=X(1))
    im = im.resize((W, H), Image.LANCZOS)
    if not cm32l:
        lcd_dots(im, ("\xff 2 \xff \xff 5 \xff |Vol: 51" if example else "").ljust(20))   # example: the photo's content
        if not base and not all_on:
            ov = glass_overlay().resize((LX1 - LX0, LY1 - LY0), Image.LANCZOS); im.paste(ov, (LX0, LY0), ov)
    return im


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
