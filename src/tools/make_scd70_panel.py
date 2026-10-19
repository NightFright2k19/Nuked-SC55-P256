#!/usr/bin/env python3
# Draws the SC-D70 interface style of the Emu-SC8820 plugin (issue #86) from scratch, modelled on
# photos and the owner's manual of the SC-D70 (silver metal front, smoked display window that narrows
# towards the bottom, red 7-segment display, two level meters, knobs and switches of the device).
# Nothing is copied from the photos; all shapes and text are drawn here, and the maker's logos and
# product names are replaced by the plugin's own branding. The SC-D70 shares the SC-8820's sound
# engine; its audio interface parts (inputs, USB audio, sample rate) are drawn but have no function.
# usage: make_scd70_panel.py <out.png|out.bgra> [--base | --all-on] [--geom]
#   --base    GUI background: lamps, segments and meters off, VOLUME / INPUT GAIN without marker,
#             no values (the GUI draws them)
#   --all-on  every lamp, segment and meter segment lit (lamp sprites of the GUI)
#   --geom    print the positions the GUI needs (editor_sc8820.inc, E70_*)
#   .bgra: raw 918x280 BGRA for gen_blob.py, else PNG
import sys, math
from PIL import Image, ImageDraw, ImageFont, ImageFilter

W, H, S = 918, 280, 3
SEP = 183                                     # upper front / lower strip
WIN, INS = (160, 29, 756, 160), 12            # display window; 12 px narrower per side at the bottom
SEG_X, SEG_Y, SEG_DX, SEG_W, SEG_H, SEG_T = 392, 102, 31, 16, 30, 3   # 7-segment digits
METERS = (520, 630)                           # USB REC LEVEL, OUTPUT LEVEL: first segment x
MET_DX, MET_W, MET_ROWS, MET_N = 10.5, 6.2, (94, 131), 9
LAMP_SRC = [(184, 98 + i * 13) for i in range(4)]      # INPUT, MIC/GUITAR, DIGITAL, INST+WAVE
LAMP_GEN = [(322, 98 + i * 13) for i in range(4)]      # PART, INST, GS, GM
LAMP_USB, LAMP_MIDI = (398, 79), (452, 79)
KNOB_VOL, KNOB_IN, KNOB_INST, KNOB_USB, KNOB_MON = (826, 112, 50), (409, 222, 22), (503, 222, 22), (590, 222, 22), (780, 222, 22)
BTN = {"PART": (37, 93, 81, 108), "INST": (83, 93, 127, 108), "DEC": (37, 142, 81, 157), "INC": (83, 142, 127, 157)}
SETUP = (652, 31, 744, 54)

base = '--base' in sys.argv
all_on = '--all-on' in sys.argv
DJ = "/usr/share/fonts/truetype/dejavu/DejaVuSans"
def F(sz, st=""): return ImageFont.truetype(DJ + st + ".ttf", int(sz * S))
def X(v): return int(round(v * S))
def lerp(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))
def tw(t, f): return f.getlength(t) / S

img = Image.new("RGB", (W * S, H * S)); d = ImageDraw.Draw(img, "RGBA")
for y in range(H * S):                        # silver front, lower strip a little darker
    t = y / (H * S)
    c = lerp((214, 216, 219), (184, 186, 190), t) if y < X(SEP) else \
        lerp((196, 198, 202), (166, 168, 172), (y - X(SEP)) / X(H - SEP))
    d.line([(0, y), (W * S, y)], fill=c)
d.rectangle([0, X(SEP) - 2, W * S, X(SEP) + 2], fill=(112, 114, 118))
d.rectangle([0, X(SEP) + 3, W * S, X(SEP) + 5], fill=(236, 238, 241))
INK, LAB, LED = (48, 50, 54), (214, 214, 218), (255, 150, 40)
def txt(x, y, s, f, fill=INK, anchor="la"): d.text((X(x), X(y)), s, font=f, fill=fill, anchor=anchor)

# display window: bezel, smoked glass
def trap(g): return [(X(WIN[0] - g), X(WIN[1] - g)), (X(WIN[2] + g), X(WIN[1] - g)),
                     (X(WIN[2] - INS + g), X(WIN[3] + g)), (X(WIN[0] + INS - g), X(WIN[3] + g))]
d.polygon(trap(7), fill=(150, 152, 156)); d.polygon(trap(5), fill=(238, 240, 243)); d.polygon(trap(0), fill=(24, 25, 29))
OFF = (70, 70, 74); RED = (255, 70, 50); GRN = (90, 235, 90); AMB = (255, 175, 40)
def lamp(x, y, on, col):
    if on:
        for r, a in ((6, 40), (4.5, 70)): d.ellipse([X(x - r), X(y - r), X(x + r), X(y + r)], fill=col + (a,))
    d.rectangle([X(x - 3.5), X(y - 1.6), X(x + 3.5), X(y + 1.6)], fill=col if on else OFF)
txt(176, 36, "Emu-", F(15, "-Oblique"), (235, 235, 238)); txt(213, 36, "SC-D70", F(15, "-Bold"), (245, 245, 248))
txt(300, 41, "DIGITAL", F(7.5, "-Bold"), (190, 190, 196))
for i, nm in enumerate(("INPUT", "MIC/GUITAR", "DIGITAL", "INST+WAVE")):
    lamp(*LAMP_SRC[i], all_on, RED); txt(LAMP_SRC[i][0] + 8, LAMP_SRC[i][1], nm, F(8.2), LAB, "lm")
for i, nm in enumerate(("PART", "INST", "GS", "GM")):
    lamp(*LAMP_GEN[i], all_on, RED); txt(LAMP_GEN[i][0] + 8, LAMP_GEN[i][1], nm, F(8.2), LAB, "lm")
# 7-segment display
d.rectangle([X(380), X(94), X(492), X(142)], fill=(14, 8, 8))
def seg_rects(x, y, w=SEG_W, h=SEG_H, th=SEG_T):
    return {"a": (x + th, y, x + w - th, y + th), "b": (x + w - th, y + th, x + w, y + h / 2 - 1),
            "c": (x + w - th, y + h / 2 + 1, x + w, y + h - th), "d": (x + th, y + h - th, x + w - th, y + h),
            "e": (x, y + h / 2 + 1, x + th, y + h - th), "f": (x, y + th, x + th, y + h / 2 - 1),
            "g": (x + th, y + h / 2 - th / 2, x + w - th, y + h / 2 + th / 2)}
for i in range(3):
    for k, b in seg_rects(SEG_X + i * SEG_DX, SEG_Y).items():
        d.rectangle([X(v) for v in b], fill=(255, 40, 30) if all_on else (60, 14, 12))
lamp(*LAMP_USB, all_on, AMB); txt(LAMP_USB[0] + 8, LAMP_USB[1], "USB", F(8.2, "-Bold"), LAB, "lm")
lamp(*LAMP_MIDI, all_on, GRN); txt(LAMP_MIDI[0] + 8, LAMP_MIDI[1], "MIDI", F(8.2, "-Bold"), LAB, "lm")
def meter(x0, title):
    txt(x0 + 44, 76, title, F(7.4, "-Bold"), LAB, "mm"); txt(x0 - 9, 100, "L", F(8), LAB, "mm"); txt(x0 - 9, 137, "R", F(8), LAB, "mm")
    for yy in MET_ROWS:
        for k in range(MET_N):
            col = GRN if k < 6 else (AMB if k < 8 else RED)
            c = col if all_on else tuple(int(v * 0.22) + 18 for v in col)
            d.rectangle([X(x0 + k * MET_DX), X(yy), X(x0 + k * MET_DX + MET_W), X(yy + 13)], fill=c)
    for k in range(6): d.ellipse([X(x0 + 3 + k * 14.5), X(116.5), X(x0 + 4.4 + k * 14.5), X(118)], fill=(180, 180, 185))
    txt(x0 + 83, 117, "OL", F(6.6), LAB, "lm")
meter(METERS[0], "USB REC LEVEL"); meter(METERS[1], "OUTPUT LEVEL")
# plugin additions, top right of the window: VOICES (count centred between label and "of N"), UNITS, SETUP
fl, fv, fo = F(9.2, "-Bold"), F(14, "-Bold"), F(8.6)
x = 404; YC = 42; GAP = 10
txt(x, YC, "VOICES", fl, LAB, "lm"); x += tw("VOICES", fl) + GAP
VOICES_X0 = x; nw = tw("000", fv); x += nw + GAP; VOICES_X1 = x - GAP; OF_X = x
x += tw("of 256", fo) + 24
txt(x, YC, "UNITS", fl, LAB, "lm"); x += tw("UNITS", fl) + GAP; UNITS_X = x
d.rectangle([X(SETUP[0]), X(SETUP[1]), X(SETUP[2]), X(SETUP[3])], fill=(10, 10, 12), outline=(78, 78, 82), width=2)
# soft gloss running down from the top edge of the window
gl = Image.new("RGBA", img.size, (0, 0, 0, 0)); gd = ImageDraw.Draw(gl)
for k in range(X(52)):
    gd.line([(X(WIN[0]) + 4, X(WIN[1]) + k), (X(WIN[2]) - 4, X(WIN[1]) + k)], fill=(255, 255, 255, int(34 * (1 - k / X(52)) ** 1.6)))
mask = Image.new("L", img.size, 0); ImageDraw.Draw(mask).polygon(trap(0), fill=255)
img.paste(Image.alpha_composite(img.convert("RGBA"), gl).convert("RGB"), (0, 0), mask); d = ImageDraw.Draw(img, "RGBA")

def button(x0, y0, x1, y1):
    d.rounded_rectangle([X(x0) + 3, X(y0) + 5, X(x1) + 4, X(y1) + 6], radius=X(3), fill=(0, 0, 0, 70))
    d.rounded_rectangle([X(x0) - 2, X(y0) - 2, X(x1) + 2, X(y1) + 2], radius=X(3), fill=(96, 98, 102))
    for k in range(X(y1 - y0)):
        d.line([(X(x0), X(y0) + k), (X(x1), X(y0) + k)], fill=lerp((242, 243, 245), (182, 184, 188), k / X(y1 - y0)))
    d.line([(X(x0) + 3, X(y0) + 2), (X(x1) - 3, X(y0) + 2)], fill=(255, 255, 255), width=3)
    d.line([(X(x0) + 2, X(y1) - 2), (X(x1) - 2, X(y1) - 2)], fill=(130, 132, 136), width=3)
for nm, (x0, y0, x1, y1) in BTN.items():
    button(x0, y0, x1, y1); txt((x0 + x1) / 2, y0 - 6, nm, F(7.6, "-Bold"), INK, "mm")
txt(37, 55, "Emu", F(20, "-BoldOblique"), (60, 62, 66)); txt(88, 62, "P256", F(11, "-Bold"), (100, 102, 108))

def knob(cx, cy, r, ang=None):
    global img, d
    sh = Image.new("RGBA", img.size, (0, 0, 0, 0))
    ImageDraw.Draw(sh).ellipse([X(cx - r + 3), X(cy - r + 6), X(cx + r + 6), X(cy + r + 9)], fill=(0, 0, 0, 90))
    img = Image.alpha_composite(img.convert("RGBA"), sh.filter(ImageFilter.GaussianBlur(X(3)))).convert("RGB")
    d = ImageDraw.Draw(img, "RGBA")
    d.ellipse([X(cx - r - 3), X(cy - r - 3), X(cx + r + 3), X(cy + r + 3)], fill=(110, 112, 116))
    n = 40 if r > 30 else 24
    for i in range(n * 2):
        d.pieslice([X(cx - r), X(cy - r), X(cx + r), X(cy + r)], i * 180 / n, (i + 1) * 180 / n,
                   fill=(206, 208, 212) if i % 2 == 0 else (150, 152, 156))
    ri = r * 0.80; lx, ly = cx - ri * 0.30, cy - ri * 0.35
    for k in range(int(ri * S), 0, -1):
        t = k / (ri * S); c = lerp((250, 251, 252), (160, 162, 166), t ** 0.9)
        ox, oy = (lx - cx) * (1 - t), (ly - cy) * (1 - t)
        d.ellipse([X(cx + ox) - k, X(cy + oy) - k, X(cx + ox) + k, X(cy + oy) + k], fill=c)
    d.arc([X(cx - ri), X(cy - ri), X(cx + ri), X(cy + ri)], 200, 300, fill=(255, 255, 255, 160), width=X(1.2))
    d.arc([X(cx - ri), X(cy - ri), X(cx + ri), X(cy + ri)], 20, 120, fill=(90, 92, 96, 150), width=X(1.2))
    if ang is not None:                       # fixed marker (knobs without a function)
        ra = math.radians(ang)
        d.line([(X(cx + ri * 0.45 * math.cos(ra)), X(cy + ri * 0.45 * math.sin(ra))),
                (X(cx + ri * 0.92 * math.cos(ra)), X(cy + ri * 0.92 * math.sin(ra)))], fill=(30, 30, 32), width=int(2.4 * S))
knob(*KNOB_VOL); txt(826, 52, "VOLUME", F(9, "-Bold"), INK, "mm")      # VOLUME and INPUT GAIN = GAIN (marker: GUI)
knob(*KNOB_IN); knob(*KNOB_INST, -76); knob(*KNOB_USB, -100); knob(*KNOB_MON, -100)
for (cx, _, _), nm in ((KNOB_IN, "INPUT GAIN"), (KNOB_INST, "INST LEVEL"), (KNOB_USB, "USB AUDIO LEVEL"), (KNOB_MON, "INPUT MONITOR")):
    txt(cx, 257, nm, F(7.6, "-Bold"), INK, "mm")
txt(KNOB_INST[0], 266, "PREVIEW (RMB)", F(6.2), INK, "mm")
button(37, 213, 113, 230); txt(75, 252, "POWER", F(7.8, "-Bold"), INK, "mm"); txt(75, 243, "\u25b2", F(6.4), INK, "mm")
d.ellipse([X(138), X(197), X(182), X(241)], fill=(120, 122, 126)); d.ellipse([X(145), X(204), X(175), X(234)], fill=(30, 30, 32))
d.ellipse([X(151), X(210), X(169), X(228)], fill=(70, 70, 74)); txt(160, 257, "MIC/GUITAR", F(7.4, "-Bold"), INK, "mm")
button(213, 207, 247, 237)                    # MIC / GUITAR: position symbols (pressed in = MIC, out = GUITAR)
d.rectangle([X(213), X(245.2), X(221), X(246.6)], fill=INK); d.rectangle([X(212), X(247.2), X(222), X(248.2)], fill=INK)
d.rectangle([X(214), X(252.2), X(220), X(256.4)], fill=INK); d.rectangle([X(212), X(256.8), X(222), X(257.8)], fill=INK)
txt(225, 247, "MIC", F(7.0, "-Bold"), INK, "lm"); txt(225, 255.5, "GUITAR", F(7.0, "-Bold"), INK, "lm")
button(314, 207, 350, 237); txt(332, 253, "REC SOURCE", F(7.4, "-Bold"), INK, "mm")
button(670, 207, 705, 237); txt(688, 247, "44.1  48", F(6.4, "-Bold"), INK, "mm"); txt(688, 257, "SAMPLE RATES", F(7.2, "-Bold"), INK, "mm")
d.ellipse([X(839), X(205), X(875), X(241)], fill=(120, 122, 126)); d.ellipse([X(846), X(212), X(868), X(234)], fill=(34, 34, 36))
d.ellipse([X(852), X(218), X(862), X(228)], fill=(80, 80, 84))
d.arc([X(851), X(249), X(863), X(262)], 180, 360, fill=INK, width=X(1.3))           # headphones symbol
d.rounded_rectangle([X(849.6), X(255.5), X(853.2), X(261.5)], radius=X(1), fill=INK)
d.rounded_rectangle([X(860.8), X(255.5), X(864.4), X(261.5)], radius=X(1), fill=INK)

if '--geom' in sys.argv:
    print(f"VOICES slot {VOICES_X0:.1f}..{VOICES_X1:.1f}, of_x {OF_X:.1f}, units_x {UNITS_X:.1f}, y {YC}")
out = img.resize((W, H), Image.LANCZOS)
if sys.argv[1].endswith('.bgra'):
    rr, g, b = out.split()
    open(sys.argv[1], 'wb').write(Image.merge('RGBA', (b, g, rr, Image.new('L', (W, H), 255))).tobytes())
else:
    out.save(sys.argv[1])
