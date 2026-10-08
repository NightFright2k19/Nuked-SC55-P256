# Erzeugt SC55-Polyphonie-Test.mid + Eventliste für die Simulation
import struct, sys, os
PPQ, BPM = 480, 120                      # 1 Tick = 1/960 s
TPS = PPQ * BPM / 60
ROUNDS = [20, 40, 64, 96, 128, 160, 200, 240, 300]
PROBE_CH, PROBE_NOTE = int(os.environ.get('PCH','8')), 72              # Kanal 9, C5
FLOOD_CH = [0, 1, 2, 3, 4, 5, 6, 7]       # Kanäle 1-8
FLOOD_KEYS = [k for k in range(36, 97)]   # C2..C7

ev = []  # (sekunden, prio, bytes|('meta',typ,text))
def at(t, data, prio=1): ev.append((t, prio, data))
def meta(t, typ, text): ev.append((t, 0, ('meta', typ, text)))

# Setup
meta(0, 0x03, "SC-55 Polyphonie-Test")
gs_reset = [0xF0,0x41,0x10,0x42,0x12,0x40,0x00,0x7F,0x00,0x41,0xF7]
at(0.0, gs_reset)
if os.environ.get("RES0", "1") == "1":
    d=[0x40,0x01,0x10]+[0]*16; cs=(128-sum(d)%128)%128
    at(0.5,[0xF0,0x41,0x10,0x42,0x12]+d+[cs,0xF7])
for ch in FLOOD_CH + [PROBE_CH]:
    at(1.0, [0xC0|ch, 16])                      # Organ 1: 1 Partial, kein Ausklingen
    at(1.0, [0xB0|ch, 91, 0]); at(1.0, [0xB0|ch, 93, 0])  # Reverb/Chorus aus -> klare Stille
    at(1.0, [0xB0|ch, 10, 64])
    at(1.0, [0xB0|ch, 7, int(os.environ.get('PVOL','110')) if ch == PROBE_CH else 38])

t = 2.0
for r, n in enumerate(ROUNDS, 1):
    meta(t, 0x06, f"Runde {r}: {n} Stimmen")
    # Ansage: r Klicks (Claves, Kanal 10)
    for k in range(r):
        at(t + k*0.18, [0x99, 75, 110]); at(t + k*0.18 + 0.05, [0x89, 75, 0])
    t += r*0.18 + 0.8
    # 1) Prüfton (älteste Note) 2) Flut aus n-1 weiteren Noten über 1,5 s
    at(t, [0x90|PROBE_CH, PROBE_NOTE, 100])
    notes = []
    i = 0
    while len(notes) < n - 1:
        ch = FLOOD_CH[i % len(FLOOD_CH)]
        key = FLOOD_KEYS[(i // len(FLOOD_CH) * 7 + i) % len(FLOOD_KEYS)]
        if (ch, key) not in notes and (ch, key) != (PROBE_CH, PROBE_NOTE):
            notes.append((ch, key))
        i += 1
    for j, (ch, key) in enumerate(notes):
        at(t + 0.2 + 1.5 * j / len(notes), [0x90|ch, key, 80])
    t += 0.2 + 1.5 + 1.5                         # voller Akkord 1,5 s halten
    meta(t, 0x06, f"Runde {r}: Prüfton allein")
    for ch, key in notes:
        at(t, [0x80|ch, key, 0])                 # alles los außer Prüfton
    probe_start = t
    t += 2.5
    at(t, [0x80|PROBE_CH, PROBE_NOTE, 0])
    print(f"probe {r} {n} {probe_start:.3f} {t:.3f}", file=sys.stderr)
    t += 1.2
meta(t, 0x06, "Ende"); at(t + 1.0, [0xB0, 121, 0])

ev.sort(key=lambda e: (e[0], e[1]))

# --- SMF Typ 0 schreiben
def vlq(v):
    out = [v & 0x7F]; v >>= 7
    while v: out.insert(0, (v & 0x7F) | 0x80); v >>= 7
    return bytes(out)
trk = bytearray(); last = 0
trk += vlq(0) + bytes([0xFF, 0x51, 3]) + (500000).to_bytes(3, 'big')  # 120 BPM
for tsec, _, d in ev:
    tick = round(tsec * TPS); trk += vlq(tick - last); last = tick
    if isinstance(d, tuple):
        txt = d[2].encode('latin-1'); trk += bytes([0xFF, d[1]]) + vlq(len(txt)) + txt
    elif d[0] == 0xF0:
        trk += bytes([0xF0]) + vlq(len(d) - 1) + bytes(d[1:])
    else:
        trk += bytes(d)
trk += vlq(0) + bytes([0xFF, 0x2F, 0])
with open("SC55-Polyphonie-Test.mid", "wb") as f:
    f.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, PPQ))
    f.write(b"MTrk" + struct.pack(">I", len(trk)) + trk)
# Eventliste für die Simulation (Sekunden, Hexbytes)
with open("events.txt", "w") as f:
    for tsec, _, d in ev:
        if not isinstance(d, tuple):
            f.write(f"{round(tsec*TPS)/TPS:.6f} {len(d)} " + " ".join(map(str, d)) + "\n")
print(f"Dauer: {t+1:.0f} s, Events: {len(ev)}", file=sys.stderr)
