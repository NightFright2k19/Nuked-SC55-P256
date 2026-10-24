#pragma once
// Nuked SC-55 Poly - XG compatibility layer for the SC-8820 (issue #95)
//
// The SC-8820 understands GS and GM2, not Yamaha XG. While XG mode is on, this translates the
// MIDI that comes in before it reaches the units:
//  - Bank Select (CC 0 / CC 32) is held back per part and applied at the next Program Change, as
//    in XG. XG MSB 0 = melody, 64 = SFX voices, 126 = SFX kit, 127 = drum kit. The unit gets the
//    GS form instead: CC 0 = variation, CC 32 = 4 (SC-8820 map), then the program. CC 32 is never
//    passed on as it came: on the SC-8820 it selects the sound map, in XG it is the variation.
//  - XG drum kits go to the nearest SC-8820 drum set, and the part is switched to a drum part
//    (GS "Use for Rhythm Part") or back to a normal part, as the bank requires.
//  - XG SysEx (F0 43 1n 4C ..): System On becomes a GS Reset; Master Volume, Part Mode and Drum
//    Part Protect are translated; everything else is dropped (not understood by the unit).
//  - GM On / GM Off / GM2 On / GS Reset from the host end XG mode.
//  - XG effects: reverb type / return, chorus type / return and the part sends are translated to the
//    GS reverb / chorus macros and CC 91 / 93. The variation block as insertion effect on one part goes to
//    the GS insertion effect (EFX) of that part, with the type and settings fitted to S-MU2000 recordings
//    (InsertionEffect); as system effect (CC 94) it is dropped, the S-MU2000 plays it inaudibly.
//  - XG variation voices (LSB) and SFX voices (MSB 64) go to the SC-8820 variation measured closest to an
//    S-MU2000 recording (xg_voice_table.h); voices without a clearly closer variation play as the basic voice.
//
// Pure functions of the incoming message and the translator's own per-part state, so it can be
// tested without an emulator (tools/test/xg_translate_test.cpp).

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>

#include "xg_voice_table.h"

namespace XgTranslate {

constexpr int kMaxPorts = 4;
constexpr int kMaxParts = kMaxPorts * 16;
constexpr uint8_t kSc8820Map = 4; // Bank Select LSB: SC-8820 map (SC-8820 manual, Bank Select LSB)

// One message to send to the units (channel message or complete SysEx)
struct Msg {
	std::array<uint8_t, 12> b{};
	uint8_t n = 0;
};

struct Out {
	std::array<Msg, 12> m{};
	int count = 0;
	bool pass = false; // send the incoming message unchanged
	void Add(std::initializer_list<uint8_t> bytes)
	{
		if (count >= static_cast<int>(m.size())) return;
		auto& x = m[static_cast<size_t>(count++)];
		x.n = 0;
		for (uint8_t v : bytes) x.b[x.n++] = v;
	}
	// Roland DT1 (F0 41 10 42 12 a1 a2 a3 d cs F7), device ID 10H as the other resets of the plug-in
	void AddDt1(uint8_t a1, uint8_t a2, uint8_t a3, uint8_t d)
	{
		const auto cs = static_cast<uint8_t>((128 - ((a1 + a2 + a3 + d) & 0x7f)) & 0x7f);
		Add({0xf0, 0x41, 0x10, 0x42, 0x12, a1, a2, a3, d, cs, 0xf7});
	}
	void AddDt1(uint8_t a1, uint8_t a2, uint8_t a3, std::array<uint8_t, 2> d)
	{
		const auto cs = static_cast<uint8_t>((128 - ((a1 + a2 + a3 + d[0] + d[1]) & 0x7f)) & 0x7f);
		Add({0xf0, 0x41, 0x10, 0x42, 0x12, a1, a2, a3, d[0], d[1], cs, 0xf7});
	}
};

// XG drum kit (Program Change, 0-based, Bank MSB 127) to SC-8820 drum set (Program Change, 0-based).
// XG numbers: SC-8820 manual p.127 and XG Specifications V1.32 table 2; matches for the GM-style
// kits (Room 8, Power/Rock 16, Electronic 24, TR-808/Analog 25, Jazz 32, Brush 40, Orchestra 48).
constexpr uint8_t DrumKit(int prog)
{
	switch (prog) {
	case 1: return 1;                    // Standard 2
	case 8: case 9: return 8;            // Room, Dark Room
	case 16: case 17: return 16;         // Rock, Rock 2 -> Power
	case 24: return 24;                  // Electro -> Electronic
	case 25: case 26: return 25;         // Analog, Analog 2 -> TR-808
	case 27: return 26;                  // Dance
	case 28: return 9;                   // Hip Hop
	case 29: return 10;                  // Jungle
	case 30: case 31: return 11;         // Apogee, Perigee -> Techno
	case 32: case 33: return 32;         // Jazz, Jazz 2
	case 40: case 41: return 40;         // Brush, Brush 2
	case 48: return 48;                  // Symphony -> Orchestra
	default: return 0;                   // Standard, Dry, Bright, ... and unknown kits -> Standard 1
	}
}

// XG SFX kit (Bank MSB 126) to SC-8820 drum set
constexpr uint8_t SfxKit(int prog)
{
	switch (prog) {
	case 1: return 60;                       // SFX Kit 2 -> SFX 2
	case 16: case 17: case 18: return 11;    // Techno kits -> Techno
	case 32: case 33: return 49;             // Sakura, Small Latin -> Ethnic
	default: return 56;                      // SFX Kit 1 and unknown kits -> SFX
	}
}

// SC-8820 voice (variation = Bank Select MSB, program) for an XG melody voice (bank MSB 0 / LSB) or SFX voice
// (MSB 64): from the measured table in xg_voice_table.h, else the basic voice of the same program.
struct Voice {
	uint8_t variation, prog;
};
constexpr Voice MelodyVoice(int xg_msb, int xg_lsb, int prog)
{
	if (xg_msb == 64) {
		const auto& e = kSfxTable[static_cast<size_t>(prog & 0x7f)];
		return e[0] == 0xff ? Voice{0, static_cast<uint8_t>(prog)} : Voice{e[0], e[1]};
	}
	if (xg_msb != 0 || xg_lsb == 0) return {0, static_cast<uint8_t>(prog)};
	size_t lo = 0, hi = kVoiceTable.size(); // binary search on (lsb, prog)
	const int key = xg_lsb * 128 + prog;
	while (lo < hi) {
		const size_t mid = (lo + hi) / 2;
		const int k      = kVoiceTable[mid].lsb * 128 + kVoiceTable[mid].prog;
		if (k == key) return {kVoiceTable[mid].variation, static_cast<uint8_t>(prog)};
		if (k < key) lo = mid + 1;
		else hi = mid;
	}
	return {0, static_cast<uint8_t>(prog)};
}

// GS reverb settings for an XG reverb type (MSB, LSB); macro -1 = no matching type. Measured against S-MU2000
// recordings of every type (tools: decay curve of a snare hit, fitted over GS macro x time x level; error under 1.5 dB
// except Tunnel 2 dB and Canyon 15 dB, whose decay is longer than the GS reverb can reach).
// GS macro (40 01 30): 0 Room 1, 1 Room 2, 2 Room 3, 3 Hall 1, 4 Hall 2, 5 Plate; time = 40 01 34, level = 40 01 33.
struct Reverb {
	int macro, time, level;
};
constexpr Reverb ReverbPreset(int msb, int lsb)
{
	switch (msb) {
	case 1: return lsb == 0 ? Reverb{4, 48, 96} : Reverb{3, 80, 48};                  // Hall 1, Hall 2
	case 2: return lsb == 0 ? Reverb{0, 24, 96} : Reverb{0, 64, 96};                  // Room 1, Room 2 / 3
	case 3: return lsb == 0 ? Reverb{3, 64, 64} : Reverb{4, 24, 96};                  // Stage 1, 2
	case 4: return Reverb{5, 96, 64};                                                 // Plate
	case 16: return Reverb{2, 48, 64};                                                // White Room
	case 17: return Reverb{3, 96, 96};                                                // Tunnel
	case 18: return Reverb{3, 112, 96};                                               // Canyon
	case 19: return Reverb{0, 24, 64};                                                // Basement
	default: return Reverb{-1, 0, 0};
	}
}

// GS chorus macro (40 01 38) for an XG chorus type (MSB, LSB); -1 = no matching GS type.
// GS: 0-3 Chorus 1-4, 4 Feedback Chorus, 5 Flanger, 6 Short Delay, 7 Short Delay (FB).
constexpr int ChorusMacro(int msb, int lsb)
{
	switch (msb) {
	case 0x41: return lsb < 4 ? lsb : 3;  // Chorus 1-4
	case 0x42: case 0x44: return 4;       // Celeste, Symphonic -> Feedback Chorus
	case 0x43: case 0x48: return 5;       // Flanger, Phaser -> Flanger
	case 0x57: return 3;                  // Ensemble Detune -> Chorus 4
	default: return -1;
	}
}

// XG variation type (MSB) as insertion effect -> GS insertion effect (EFX) type and fixed settings, fitted to an
// S-MU2000 recording of each type on four voices (xg_insertion_cal.mid, issue #95). Types without a GS type
// closer to the recording than no effect (Phaser, Echo, the rest) play without an insertion effect.
enum class Efx : uint8_t { None, Overdrive, AutoWah, StereoDelay, Eq };
constexpr Efx InsertionEffect(int msb)
{
	switch (msb) {
	case 0x49: case 0x4a: case 0x4b: return Efx::Overdrive; // Distortion, Overdrive, Amp Simulator
	case 0x4e: return Efx::AutoWah;
	case 0x05: case 0x06: case 0x08: return Efx::StereoDelay; // Delay L,C,R / L,R, Cross Delay
	case 0x4c: return Efx::Eq;                                 // 3-Band EQ
	default: return Efx::None;
	}
}
// XG EQ frequency index (20 Hz * 2^(i/6), XG EQ frequency table) -> GS Stereo-EQ M1 Freq value: measured on the
// SC-8820 as 1320 Hz * 2^((v - 64) / 26)
constexpr uint8_t EqMidFreq(int idx)
{
	const int v = (13 * idx - 280) / 3;
	return static_cast<uint8_t>(v < 0 ? 0 : (v > 127 ? 127 : v));
}
constexpr uint8_t EqGain(int v) { return static_cast<uint8_t>(v < 52 ? 52 : (v > 76 ? 76 : v)); } // -12..+12 dB, both
// XG EQ Mid Width (10-120 = Q 1.0-12.0) -> GS M1 Q (0.5 / 1 / 2 / 4 / 9)
constexpr uint8_t EqQ(int w) { return static_cast<uint8_t>(w <= 15 ? 1 : (w <= 30 ? 2 : (w <= 60 ? 3 : 4))); }

// GS part block x (the "1x" in 40 1x 15) of a MIDI channel
constexpr uint8_t PartBlock(int ch) { return static_cast<uint8_t>(ch == 9 ? 0 : (ch < 9 ? ch + 1 : ch)); }

class Translator {
public:
	bool Enabled() const { return enabled; }
	void Disable() { enabled = false; }
	void Enable()
	{
		enabled = true;
		Reset();
	}
	// the XG defaults (Hall 1 reverb, Chorus 1) are sent with the first channel message after a reset: the GS Reset
	// that XG System On becomes holds back the host's events, so these follow it in order

	// XG default state (XG System On): channel 10 is the drum part, bank 0 everywhere, protect on
	void Reset()
	{
		protect = true;
		rev_pending = cho_pending = true;
		rev_base = ReverbPreset(1, 0).level;
		rev_return = 64;
		var_type = 0x05; // XG default variation: Delay L,C,R, connection insertion, no part
		var_conn = 0;
		var_part = 127;
		efx_part = -1;
		var_param = kVarDefaults;
		for (int c = 0; c < 16; ++c) send_rev[c] = 40, send_cho[c] = 0;
		for (int p = 0; p < kMaxParts; ++p) {
			const bool drum = (p % 16) == 9;
			msb[p]    = drum ? 127 : 0;
			lsb[p]    = 0;
			rhythm[p] = drum;
		}
	}

	// Channel message of the host (3 bytes for CC, 2 for Program Change). Only while enabled.
	void Short(const uint8_t* d, size_t len, int port, Out& out)
	{
		out.count = 0;
		out.pass  = true;
		if (!enabled || len < 2 || port < 0 || port >= kMaxPorts) return;
		const int status = d[0] & 0xf0, ch = d[0] & 0x0f, part = port * 16 + ch;
		if (rev_pending) SendReverb(1, 0, out);
		if (cho_pending) SendChorus(0x41, 0, out);
		if (status == 0xb0 && len >= 3 && d[1] == 94) { // variation send: the S-MU2000 plays no variation delay, the GS delay would
			out.pass = false;
			return;
		}
		if (status == 0xb0 && len >= 3 && port == 0 && (d[1] == 91 || d[1] == 93)) { // part sends, also for the EFX output
			SetSend(ch, d[1] == 91, d[2] & 0x7f, out);
			return;
		}
		if (status == 0xb0 && len >= 3 && (d[1] == 0 || d[1] == 32)) {
			out.pass = false; // held back until the next Program Change
			if (d[1] == 32) {
				lsb[part] = d[2] & 0x7f;
			} else {
				const int v = d[2] & 0x7f;
				// Drum Part Protect: channel 10 only takes the drum banks
				if (!(ch == 9 && protect && v != 126 && v != 127)) msb[part] = static_cast<uint8_t>(v);
			}
			return;
		}
		if (status != 0xc0) return;
		out.pass = false;
		const int prog = d[1] & 0x7f, bank = msb[part];
		const auto ch8 = static_cast<uint8_t>(ch);
		if (bank == 127 || bank == 126) {
			if (!rhythm[part]) SetRhythm(part, ch, ch == 9 ? 1 : 2, out);
			out.Add({static_cast<uint8_t>(0xb0 | ch8), 0, 0});
			out.Add({static_cast<uint8_t>(0xb0 | ch8), 32, kSc8820Map});
			out.Add({static_cast<uint8_t>(0xc0 | ch8), bank == 127 ? DrumKit(prog) : SfxKit(prog)});
		} else {
			if (rhythm[part]) SetRhythm(part, ch, 0, out);
			const Voice v = MelodyVoice(bank, lsb[part], prog);
			out.Add({static_cast<uint8_t>(0xb0 | ch8), 0, v.variation});
			out.Add({static_cast<uint8_t>(0xb0 | ch8), 32, kSc8820Map});
			out.Add({static_cast<uint8_t>(0xc0 | ch8), v.prog});
		}
	}

	// SysEx of the host (complete message with F0 .. F7). Works while disabled too (XG System On
	// switches XG mode on; GM / GS resets switch it off). On return: out.pass = send the message as
	// it is; else out holds the replacement (count 0 = drop it).
	void SysEx(std::span<const uint8_t> m, int port, Out& out)
	{
		out.count = 0;
		out.pass  = true;
		if (m.size() >= 6 && m[1] == 0x7e && m[3] == 0x09 && m[4] >= 1 && m[4] <= 3) {
			enabled = false; // GM On / GM Off / GM2 On
			return;
		}
		if (m.size() >= 10 && m[1] == 0x41 && m[3] == 0x42 && m[4] == 0x12 && m[5] == 0x40 &&
		    m[6] == 0x00 && m[7] == 0x7f) {
			enabled = false; // GS Reset
			return;
		}
		if (!(m.size() >= 8 && m[1] == 0x43 && (m[2] & 0xf0) == 0x10 && m[3] == 0x4c)) return; // not XG
		out.pass = false;
		const uint8_t a1 = m[4], a2 = m[5], a3 = m[6];
		if (m.size() >= 9 && a1 == 0x00 && a2 == 0x00 && a3 == 0x7e) { // XG System On -> GS Reset
			Enable();
			out.AddDt1(0x40, 0x00, 0x7f, 0x00);
			return;
		}
		if (!enabled || m.size() < 9) return; // other XG messages are dropped; the translation needs XG mode
		const uint8_t data = m[7];
		if (a1 == 0x00 && a2 == 0x00 && a3 == 0x07) { // Drum Part Protect
			protect = data != 0;
		} else if (a1 == 0x00 && a2 == 0x00 && a3 == 0x04) { // Master Volume -> GS 40 00 04
			out.AddDt1(0x40, 0x00, 0x04, data & 0x7f);
		} else if (a1 == 0x02 && a2 == 0x01 && a3 == 0x00 && m.size() >= 10) { // Reverb Type (MSB, LSB)
			SendReverb(m[7] & 0x7f, m[8] & 0x7f, out);
		} else if (a1 == 0x02 && a2 == 0x01 && a3 == 0x20 && m.size() >= 10) { // Chorus Type (MSB, LSB)
			SendChorus(m[7] & 0x7f, m[8] & 0x7f, out);
		} else if (a1 == 0x02 && a2 == 0x01 && a3 == 0x0c) { // Reverb Return -> GS Reverb Level
			rev_return = data & 0x7f;
			out.AddDt1(0x40, 0x01, 0x33, ReverbLevel());
		} else if (a1 == 0x02 && a2 == 0x01 && a3 == 0x2c) { // Chorus Return -> GS Chorus Level
			out.AddDt1(0x40, 0x01, 0x3a, data & 0x7f);
		} else if (a1 == 0x02 && a2 == 0x01 && a3 == 0x40 && m.size() >= 10) { // Variation Type: XG loads its defaults
			var_type  = m[7] & 0x7f;
			var_param = kVarDefaults;
			UpdateEfx(true, out);
		} else if (a1 == 0x02 && a2 == 0x01 && a3 >= 0x42 && a3 <= 0x4d && (a3 & 1) == 0 && m.size() >= 10) {
			// Variation Parameter 1-6 (MSB, LSB); the EQ settings are passed on
			var_param[static_cast<size_t>((a3 - 0x42) / 2)] = m[7] ? 127 : static_cast<uint8_t>(m[8] & 0x7f);
			if (InsertionEffect(var_type) == Efx::Eq && efx_part >= 0) SendEqParams(out);
		} else if (a1 == 0x02 && a2 == 0x01 && (a3 == 0x5a || a3 == 0x5b)) { // Variation Connection / Part
			(a3 == 0x5a ? var_conn : var_part) = data & 0x7f;
			UpdateEfx(false, out);
		} else if (a1 == 0x08 && a2 < 16 && (a3 == 0x12 || a3 == 0x13)) { // Part Chorus / Reverb Send -> CC 93 / 91
			out.Add({static_cast<uint8_t>(0xb0 | a2), static_cast<uint8_t>(a3 == 0x12 ? 93 : 91), static_cast<uint8_t>(data & 0x7f)});
			if (port == 0) SetSend(a2, a3 == 0x13, data & 0x7f, out);
		} else if (a1 == 0x08 && a2 < 16 && a3 == 0x07 && port >= 0 && port < kMaxPorts) { // Part Mode
			const int part = port * 16 + a2;
			SetRhythm(part, a2, data == 0 ? 0 : (a2 == 9 ? 1 : 2), out);
			if (data != 0 && !(a2 == 9 && protect)) msb[part] = 127;
			if (data == 0 && msb[part] >= 126) msb[part] = 0;
		}
	}

private:
	uint8_t ReverbLevel() const
	{
		const int v = rev_base * rev_return / 64;
		return static_cast<uint8_t>(v > 127 ? 127 : v);
	}
	// reverb / chorus type: GS macro (and, for the reverb, time and level fitted to the XG type)
	void SendReverb(int msb, int lsb, Out& out)
	{
		const Reverb r = ReverbPreset(msb, lsb);
		rev_pending    = false;
		if (r.macro < 0) return;
		rev_base = r.level;
		out.AddDt1(0x40, 0x01, 0x30, static_cast<uint8_t>(r.macro));
		out.AddDt1(0x40, 0x01, 0x34, static_cast<uint8_t>(r.time));
		out.AddDt1(0x40, 0x01, 0x33, ReverbLevel());
	}
	void SendChorus(int msb, int lsb, Out& out)
	{
		const int t = ChorusMacro(msb, lsb);
		cho_pending = false;
		if (t >= 0) out.AddDt1(0x40, 0x01, 0x38, static_cast<uint8_t>(t));
	}

	// GS insertion effect for the XG variation block: on for the variation part while it is an insertion effect
	// (connection 0, a part of port A) of a type with a GS counterpart, else off. new_type: the type was set.
	void UpdateEfx(bool new_type, Out& out)
	{
		const Efx e    = InsertionEffect(var_type);
		const int part = (var_conn == 0 && var_part < 16 && e != Efx::None) ? var_part : -1;
		if (part != efx_part && efx_part >= 0) out.AddDt1(0x40, static_cast<uint8_t>(0x40 | PartBlock(efx_part)), 0x22, 0);
		if (part < 0) {
			efx_part = -1;
			return;
		}
		if (new_type || part != efx_part) {
			switch (e) {
			case Efx::Overdrive: // drive and output level fitted; the XG Overdrive is the milder type
				out.AddDt1(0x40, 0x03, 0x00, {0x01, 0x10});
				out.AddDt1(0x40, 0x03, 0x03, var_type == 0x4a ? 48 : 80);
				out.AddDt1(0x40, 0x03, 0x16, 28);
				break;
			case Efx::AutoWah: // manual, peak, depth
				out.AddDt1(0x40, 0x03, 0x00, {0x01, 0x21});
				out.AddDt1(0x40, 0x03, 0x05, 30);
				out.AddDt1(0x40, 0x03, 0x06, 30);
				out.AddDt1(0x40, 0x03, 0x08, 110);
				break;
			case Efx::StereoDelay: // effect balance
				out.AddDt1(0x40, 0x03, 0x00, {0x01, 0x50});
				out.AddDt1(0x40, 0x03, 0x12, 32);
				break;
			case Efx::Eq:
				out.AddDt1(0x40, 0x03, 0x00, {0x01, 0x00});
				break;
			case Efx::None: break;
			}
			if (e == Efx::Eq) SendEqParams(out);
		}
		if (part != efx_part || new_type) {
			out.AddDt1(0x40, 0x03, 0x17, send_rev[static_cast<size_t>(part)]);
			out.AddDt1(0x40, 0x03, 0x18, send_cho[static_cast<size_t>(part)]);
			out.AddDt1(0x40, static_cast<uint8_t>(0x40 | PartBlock(part)), 0x22, 1);
		}
		efx_part = part;
	}
	// XG 3-Band EQ (low gain, mid frequency, mid gain, mid width, high gain, low frequency) -> GS Stereo-EQ
	// (Low Freq 200 / 400 Hz, Low Gain, Hi Gain, M1 Freq, M1 Q, M1 Gain; Hi Freq stays at its default)
	void SendEqParams(Out& out)
	{
		const auto& p = var_param;
		out.AddDt1(0x40, 0x03, 0x03, p[5] < 25 ? 0 : 1);
		out.AddDt1(0x40, 0x03, 0x04, EqGain(p[0]));
		out.AddDt1(0x40, 0x03, 0x06, EqGain(p[4]));
		out.AddDt1(0x40, 0x03, 0x07, EqMidFreq(p[1]));
		out.AddDt1(0x40, 0x03, 0x08, EqQ(p[3]));
		out.AddDt1(0x40, 0x03, 0x09, EqGain(p[2]));
		out.AddDt1(0x40, 0x03, 0x0c, 64); // M2 Gain flat: its default cuts the mid range
	}
	void SetSend(int ch, bool reverb, uint8_t v, Out& out)
	{
		(reverb ? send_rev : send_cho)[static_cast<size_t>(ch)] = v;
		if (ch == efx_part) out.AddDt1(0x40, 0x03, reverb ? 0x17 : 0x18, v);
	}

	// GS "Use for Rhythm Part" (40 1x 15): 0 = normal part, 1 = drum part (map 1), 2 = drum part (map 2)
	void SetRhythm(int part, int ch, int mode, Out& out)
	{
		rhythm[part] = mode != 0;
		out.AddDt1(0x40, static_cast<uint8_t>(0x10 | PartBlock(ch)), 0x15, static_cast<uint8_t>(mode));
	}

	bool enabled = false;
	bool protect = true;
	bool rev_pending = false, cho_pending = false; // XG default effect not sent yet
	int rev_base = 96, rev_return = 64;            // GS reverb level of the current type, XG reverb return
	std::array<uint8_t, kMaxParts> msb{}, lsb{};
	std::array<bool, kMaxParts> rhythm{};
	// XG variation block (type MSB, connection, part, parameters 1-6) and the part on the GS insertion effect;
	// part sends of port A, also used as the EFX output sends
	static constexpr std::array<uint8_t, 6> kVarDefaults{70, 34, 64, 10, 64, 28}; // 3-Band EQ, measured / XG table
	uint8_t var_type = 0x05, var_conn = 0, var_part = 127;
	std::array<uint8_t, 6> var_param = kVarDefaults;
	int efx_part = -1;
	std::array<uint8_t, 16> send_rev{}, send_cho{};
};

} // namespace XgTranslate
