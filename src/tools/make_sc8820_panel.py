#!/usr/bin/env python3
# Draws the Emu-SC8820 panel from scratch, modelled on photos of the SC-8820 front (champagne
# metal front, smoked display window, 12 x 3 level dots per part group: columns 1-8 green, 9 yellow,
# 10-12 red with blends in columns 7, 8, 10, 11; red POWER and amber USB symbols). Nothing is copied from the photos; all shapes and text are
# drawn here, and the maker's logos and product names are replaced by the plugin's own branding.
# Upper 196 px = the device front; lower strip = plugin additions (VOICES, UNITS, SETUP).
# usage: make_sc8820_panel.py <out.png|out.bgra> [--lit | --all-on | --base]
#   --lit     example lamp state (preview)          --all-on  every lamp lit (lamp sprites of the GUI)
#   --base    GUI background: lamps off, knob without marker, boxes empty (the GUI draws them)
#   --pro     Pro interface (SETUP "Interface Style"): 16-part level display in the window, part display and edit buttons
#             in the lower strip, orange "Pro" in the name (with --lit: example values for a preview)
#   .bgra: raw 918x280 BGRA for gen_blob.py, else PNG
import sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter
W, H, S = 918, 280, 3            # final size, supersampling factor
FRONT = 196                      # height of the device front
# Pro: 16-part level display (first column x, column pitch, bottom row y, row pitch)
EQ_X, EQ_DX, EQ_Y, EQ_DY = 291.5, 7.6, 104.0, 7.0
# Pro: lower-strip part display (x of each field; value examples for --lit) and edit buttons (centre x, label y)
FIELDS_1 = [(270, "PART", ("A01",)), (314, "PROG", ("001",)), (358, "INSTRUMENT", ("Piano 1",))]
FIELDS_2 = [(270, "MSB", ("000",)), (314, "LSB", ("000",)), (358, "LEVEL", ("100",)), (414, "PAN", ("0",)),
            (470, "REVERB", ("40",)), (526, "CHORUS", ("0",))]
PILLS = [(640, 202, "PART"), (736, 202, "PROGRAM"), (640, 238, "BANK MSB"), (736, 238, "BANK LSB")]
lit = '--lit' in sys.argv
all_on = '--all-on' in sys.argv
base = '--base' in sys.argv
pro = True   # issue #87: the Basic panel was removed, only the layout with the part display is left (--pro is accepted and ignored)
im = Image.new('RGB', (W*S, H*S)); d = ImageDraw.Draw(im)
DJ = "/usr/share/fonts/truetype/dejavu/DejaVuSans"
def F(sz, st=""): return ImageFont.truetype(DJ + st + ".ttf", int(sz*S))
def X(v): return int(round(v*S))
def lerp(a, b, t): return tuple(int(a[i] + (b[i]-a[i])*t) for i in range(3))

# --- metal front: champagne, light top bevel, darker lower lip
top, mid, low = (226, 216, 196), (205, 194, 172), (176, 165, 144)
for y in range(FRONT*S):
    t = y/(FRONT*S)
    c = lerp(top, mid, min(1, t/0.12)) if t < 0.12 else lerp(mid, low, (t-0.12)/0.88)
    d.line([(0, y), (W*S, y)], fill=c)
d.rectangle((0, 0, W*S, X(3)), fill=(238, 231, 215))                      # top edge highlight
d.rectangle((0, X(FRONT-14), W*S, X(FRONT)), fill=(160, 149, 128))         # lower lip
d.line([(0, X(FRONT-14)), (W*S, X(FRONT-14))], fill=(208, 198, 178), width=X(1.2))
# --- lower strip (plugin): graphite
for y in range(X(FRONT), H*S):
    t = (y - X(FRONT))/((H-FRONT)*S)
    d.line([(0, y), (W*S, y)], fill=lerp((58, 58, 60), (36, 36, 38), t))
d.line([(0, X(FRONT)), (W*S, X(FRONT))], fill=(20, 20, 20), width=X(2))

INK = (48, 46, 44)
# --- left: VOICES module like the other panels (count, level bar, UNITS) in a recessed dark bay
VX0, VX1 = 22, 160
d.rounded_rectangle((X(VX0-6), X(14), X(VX1+6), X(FRONT-22)), X(5), fill=(150, 140, 120))      # bay edge
d.rounded_rectangle((X(VX0-4), X(16), X(VX1+4), X(FRONT-24)), X(4), fill=(52, 52, 56))
def centred_in(box, text, font, fill):
    x0, y0, x1, y1 = box
    l, t, r_, b_ = d.textbbox((0, 0), text, font=font)
    d.text(((X(x0) + X(x1) - (r_ - l))/2 - l, (X(y0) + X(y1) - (b_ - t))/2 - t), text, font=font, fill=fill)
centred_in((VX0, 18, VX1, 34), "VOICES", F(11, "-Bold"), (225, 225, 225))
d.rectangle((X(VX0+2), X(37), X(VX1-2), X(132)), fill=(20, 20, 22))                          # display
VOICE_BOX = (VX0+2, 37, VX1-2, 132)
VWIN = (33, 37, 149, 132)              # window given to DrawVoices88 by the GUI (text centred, bar from +8)
if lit:
    centred_in((VWIN[0], 45, VWIN[2], 79), "031", F(30, "-Bold"), (255, 154, 46))
    centred_in((VWIN[0], 81, VWIN[2], 95), "of 256", F(9.5, "-Bold"), (154, 158, 166))
for k in range(20):                                                                            # level bar
    x = VWIN[0] + 8 + k*5
    on = lit and k < 3
    col = (255, 154, 46) if on else (90, 56, 24)
    if lit and k == 4: col = (255, 88, 48)                                                     # peak
    d.rectangle((X(x), X(103), X(x+4), X(125)), fill=col)
d.rectangle((X(36), X(140), X(146), X(160)), fill=(26, 26, 28))                               # UNITS
if lit: centred_in((36, 140, 146, 160), "UNITS 1/4", F(10.5, "-Bold"), (255, 154, 46))
# --- INST MAP button
d.text((X(184), X(76)), "INST MAP", font=F(12.5, "-Bold"), fill=INK)
cx, cy, r = 215, 112, 8.5
d.ellipse((X(cx-r-2), X(cy-r-2), X(cx+r+2), X(cy+r+2)), fill=(150, 140, 120))
d.ellipse((X(cx-r), X(cy-r), X(cx+r), X(cy+r)), fill=(168, 168, 166))
d.ellipse((X(cx-r+2), X(cy-r+1.5), X(cx+r-3), X(cy+r-4)), fill=(196, 196, 194))
# --- display window: smoked glass in a recessed frame
wx0, wy0, wx1, wy1 = 272, 42, 656, 150
d.rectangle((X(wx0-4), X(wy0-4), X(wx1+4), X(wy1+6)), fill=(150, 140, 120))
d.rectangle((X(wx0), X(wy0), X(wx1), X(wy1)), fill=(24, 25, 27))
d.rectangle((X(wx0), X(wy0), X(wx1), X(wy0+44)), fill=(30, 31, 34))      # glass reflection band
PR = (200, 202, 204)                                                       # window print
if not pro:
    d.text((X(296), X(52)), "POWER", font=F(14, "-Bold"), fill=PR)
    d.text((X(368), X(52)), "USB", font=F(14, "-Bold"), fill=PR)
else:
    d.text((X(284), X(132)), "POWER", font=F(9, "-Bold"), fill=PR)
    d.text((X(354), X(132)), "USB", font=F(9, "-Bold"), fill=PR)
d.text((X(428), X(52)), "PART A", font=F(14, "-Bold"), fill=PR)
d.text((X(428), X(90)), "PART B", font=F(14, "-Bold"), fill=PR)
if not pro:
    d.text((X(298), X(114)), "Emu", font=F(17, "-Oblique"), fill=PR)
    d.text((X(298) + d.textlength("Emu ", font=F(17, "-Oblique")), X(113)), "P256", font=F(18, "-BoldOblique"), fill=PR)
# map labels under PART B, one per 3-column segment
GX, GW = 508, 11.4                                    # first dot column, column pitch
labels = ["55", "88", "88Pro", "8820"]
d.text((X(428), X(116)), "INST MAP", font=F(8, "-Bold"), fill=PR)
for k, t in enumerate(labels):
    cxl = GX + (k*3 + 1)*GW
    w = d.textlength(t, font=F(9, "-BoldOblique"))
    d.text((X(cxl) - w/2, X(115)), t, font=F(9, "-BoldOblique"), fill=PR)
d.line([(X(428), X(129)), (X(GX + 11.5*GW + 4), X(129))], fill=PR, width=X(1))

# lamp drawing (also used by the plugin's sprite generator later)
RED, GRN, YEL = (255, 48, 36), (120, 230, 60), (240, 200, 40)
def dot(x, y, col, on):
    rr = 2.4
    if on:
        glow = Image.new('RGBA', (X(14), X(14)), (0, 0, 0, 0)); gd = ImageDraw.Draw(glow)
        gd.ellipse((X(2), X(2), X(12), X(12)), fill=col + (90,))
        glow = glow.filter(ImageFilter.GaussianBlur(X(1.6)))
        im.paste(glow, (X(x-7), X(y-7)), glow)
        d.ellipse((X(x-rr), X(y-rr), X(x+rr), X(y+rr)), fill=col)
    else:
        d.ellipse((X(x-rr), X(y-rr), X(x+rr), X(y+rr)), fill=(92, 92, 94))   # unlit: grey print
YGN, ORG = (190, 225, 50), (255, 140, 30)                 # yellow-green, orange
# colour per column (1..12) and row (top..bottom): green, then a blend through yellow and orange to red
DOT_COL = {c: (GRN, GRN, GRN) for c in range(1, 7)}
DOT_COL.update({7: (GRN, GRN, YGN), 8: (GRN, YGN, YEL), 9: (YEL, YEL, YEL),
                10: (RED, ORG, YEL), 11: (RED, RED, ORG), 12: (RED, RED, RED)})
def meter(y0, segs):                                  # segs: 4 bools
    for c in range(12):
        for r in range(3):
            dot(GX + c*GW, y0 + r*6.2, DOT_COL[c + 1][r], segs[c//3])
meter(56, [1]*4 if all_on else ([1, 1, 1, 0] if lit else [0]*4))
meter(94, [1]*4 if all_on else ([0, 0, 1, 0] if lit else [0]*4))
# POWER symbol and USB symbol (red when lit)
OFF_ICON = (118, 118, 120)
def glow(x, y, rad, col):
    g = Image.new('RGBA', (X(rad*4), X(rad*4)), (0, 0, 0, 0)); gd = ImageDraw.Draw(g)
    gd.ellipse((X(rad), X(rad), X(rad*3), X(rad*3)), fill=col + (110,))
    g = g.filter(ImageFilter.GaussianBlur(X(rad*0.45))); im.paste(g, (X(x-rad*2), X(y-rad*2)), g)
def power_icon(x, y, on):
    col = RED if on else OFF_ICON
    if on: glow(x, y, 9, RED)
    d.arc((X(x-6), X(y-6), X(x+6), X(y+6)), 300, 240, fill=col, width=X(1.6))
    d.line([(X(x), X(y-8)), (X(x), X(y-1))], fill=col, width=X(1.6))
AMB = (255, 160, 40)                                  # USB lamp: amber on the device (photos)
def usb_icon(x, y, on):
    col = AMB if on else OFF_ICON
    if on: glow(x, y, 8, AMB)
    d.line([(X(x-9), X(y)), (X(x+9), X(y))], fill=col, width=X(1.4))
    d.ellipse((X(x-11), X(y-2), X(x-7), X(y+2)), fill=col)
    d.polygon([(X(x+9), X(y-3)), (X(x+13), X(y)), (X(x+9), X(y+3))], fill=col)
    d.line([(X(x-3), X(y)), (X(x+1), X(y-4)), (X(x+5), X(y-4))], fill=col, width=X(1.2))
    d.line([(X(x-1), X(y)), (X(x+3), X(y+4)), (X(x+6), X(y+4))], fill=col, width=X(1.2))
if not pro:
    power_icon(323, 84, lit or all_on)
    usb_icon(384, 84, lit or all_on)   # shown as connected (always on in the plugin)
else:                                  # small, beside their labels
    power_icon(334, 137, lit or all_on)
    usb_icon(390, 137, lit or all_on)
# --- GAIN knob and phones jack; PREVIEW (the device's push) is a right click in the plug-in (RMB)
d.text((X(686), X(56)), "GAIN", font=F(16, "-Bold"), fill=INK)
gl, _, gr, _ = d.textbbox((X(686), X(56)), "GAIN", font=F(16, "-Bold"))
pl, _, pr, _ = d.textbbox((0, 0), "PREVIEW (RMB)", font=F(8.5, "-Bold"))
d.text(((gl + gr - pl - pr)/2, X(76)), "PREVIEW (RMB)", font=F(8.5, "-Bold"), fill=INK)   # centred below GAIN
kx, ky = 712, 118
d.ellipse((X(kx-26), X(ky-26), X(kx+26), X(ky+26)), fill=(150, 140, 120))
d.ellipse((X(kx-22), X(ky-22), X(kx+22), X(ky+22)), fill=(26, 26, 26))
d.ellipse((X(kx-15), X(ky-15), X(kx+15), X(ky+15)), fill=(40, 40, 40))
if not base: d.line([(X(kx), X(ky-21)), (X(kx), X(ky-13))], fill=(232, 214, 120), width=X(2.6))   # cream marker (GUI: drawn at the GAIN position)
for mx in (kx-29, kx+29):                                                      # min / max dots
    d.ellipse((X(mx-1.6), X(ky+22), X(mx+1.6), X(ky+25.2)), fill=INK)
GAIN_BOX = (kx-30, ky+28, kx+30, ky+42)                                         # value, drawn by the GUI
if lit: centred_in(GAIN_BOX, "+0.0 dB", F(10, "-Bold"), INK)
jx, jy = 792, 112
# headphones: headband arc and two ear cups
d.arc((X(jx-8), X(57), X(jx+8), X(75)), 180, 360, fill=INK, width=X(2.2))
for sx in (-1, 1):
    cxp = jx + sx*7.2
    d.rounded_rectangle((X(cxp-2.6), X(64), X(cxp+2.6), X(74)), X(1.4), fill=INK)
d.ellipse((X(jx-15), X(jy-15), X(jx+15), X(jy+15)), fill=(150, 140, 120))
d.ellipse((X(jx-12), X(jy-12), X(jx+12), X(jy+12)), fill=(30, 30, 30))
d.ellipse((X(jx-5), X(jy-5), X(jx+5), X(jy+5)), fill=(70, 70, 70))
# --- plugin strip: VOICES / UNITS / SETUP / branding, texts centred in their boxes
OR = (255, 140, 40)
def centred(box, text, font, fill):
    x0, y0, x1, y1 = box
    l, t, r_, b_ = d.textbbox((0, 0), text, font=font)
    d.text(((X(x0) + X(x1) - (r_ - l))/2 - l, (X(y0) + X(y1) - (b_ - t))/2 - t), text, font=font, fill=fill)
LOGO_Y = 241                         # top of "Emu-SC" (size 22): same height as the SC-55/88/8850 logos (issue #70)
# logo "Emu-SC8820" (issue #87: the former Basic logo, also on the panel with the part display)
d.text((X(40), X(LOGO_Y)), "Emu-SC", font=F(22, "-Oblique"), fill=(150, 150, 150))
x = X(40) + d.textlength("Emu-SC", font=F(22, "-Oblique")) + X(1)
d.text((x, X(LOGO_Y-2)), "8820", font=F(24, "-BoldOblique"), fill=(200, 200, 200))
SB = (812, FRONT+44, 900, FRONT+68)  # same place in Basic and Pro (issue #67)
d.rectangle(tuple(X(v) for v in SB), fill=(16, 16, 16))
if not base: centred(SB, "SETUP  >", F(11, "-Bold"), OR)

if pro:
    # --- 16-part level display (left of the window): 16 columns x 8 dots, the port of the selected part
    for c in range(16):
        for r in range(8):
            on = False
            if lit:
                lv = [7, 5, 6, 3, 0, 4, 2, 0, 5, 8, 1, 0, 3, 0, 0, 2][c]
                on = r < lv
            if all_on: on = True
            col = [GRN, GRN, GRN, GRN, GRN, YGN, YEL, RED][r]
            dot(EQ_X + c*EQ_DX, EQ_Y - r*EQ_DY, col, on)
    for c in (0, 4, 9, 15):                                          # part numbers 1, 5, 10, 16
        t = str(c + 1); w = d.textlength(t, font=F(7.5, "-Bold"))
        d.text((X(EQ_X + c*EQ_DX) - w/2, X(EQ_Y + 6)), t, font=F(7.5, "-Bold"), fill=PR)
    if lit:   # port letter and the selected part's column marker (drawn by the GUI)
        d.text((X(EQ_X - 13), X(EQ_Y + 5)), "A", font=F(8, "-Bold"), fill=OR)
        d.rectangle((X(EQ_X + 0*EQ_DX - 2.5), X(EQ_Y + 16), X(EQ_X + 0*EQ_DX + 2.5), X(EQ_Y + 17.5)), fill=OR)

    # --- lower strip: part display and the edit buttons
    LCD = (262, FRONT+10, 584, FRONT+76)
    d.rounded_rectangle(tuple(X(v) for v in (LCD[0]-3, LCD[1]-3, LCD[2]+3, LCD[3]+3)), X(4), fill=(70, 70, 74))
    d.rectangle(tuple(X(v) for v in LCD), fill=(14, 14, 15))
    LB = (150, 152, 158)                                             # label grey
    def field(x, y, label, value, w):
        d.text((X(x), X(y)), label, font=F(7.5, "-Bold"), fill=LB)
        if lit: d.text((X(x), X(y + 9)), value, font=F(14, "-Bold"), fill=OR)
    r1, r2 = LCD[1] + 4, LCD[1] + 36
    for x, lab, val in FIELDS_1: field(x, r1, lab, val[0] if lit else "", 0)
    for x, lab, val in FIELDS_2: field(x, r2, lab, val[0] if lit else "", 0)
    # edit buttons: label, then a dark pill with two light round buttons (left / right)
    def pill(cx, y, label):
        w = d.textlength(label, font=F(8.5, "-Bold"))
        d.text((X(cx) - w/2, X(y)), label, font=F(8.5, "-Bold"), fill=(205, 205, 205))
        py = y + 18
        d.rounded_rectangle((X(cx-40), X(py-10), X(cx+40), X(py+10)), X(10), fill=(22, 22, 24))
        for sx in (-1, 1):
            bx = cx + sx*19
            d.rounded_rectangle((X(bx-16), X(py-7), X(bx+16), X(py+7)), X(4), fill=(196, 196, 194))
            d.rounded_rectangle((X(bx-16), X(py-7), X(bx+16), X(py+4)), X(4), fill=(214, 214, 212))
            tri = [(bx-3, py-4), (bx-3, py+4), (bx+4, py)] if sx > 0 else [(bx+3, py-4), (bx+3, py+4), (bx-4, py)]
            d.polygon([(X(a), X(b)) for a, b in tri], fill=(40, 40, 40))
    for (cx, y, lab) in PILLS: pill(cx, y, lab)
out = im.resize((W, H), Image.LANCZOS)
if sys.argv[1].endswith('.bgra'):
    b, g, rr = out.split()[2], out.split()[1], out.split()[0]
    open(sys.argv[1], 'wb').write(Image.merge('RGBA', (b, g, rr, Image.new('L', (W, H), 255))).tobytes())
else:
    out.save(sys.argv[1])
print(sys.argv[1], W, H)
