#pragma once
// Nuked SC-55 Poly – MIDI voice router
//
// Distributes incoming MIDI over N identical emulator instances so that the
// combined polyphony is N x 24 (SC-55) or N x 28 (SC-55mk2) partials.
//
// Rules:
//  - Note-on: goes to the instance with the lowest load (measured active
//    PCM slots + note-ons sent since the last measurement).
//  - Re-trigger of a key that is still held on the same channel goes to the
//    instance that already plays it (keeps the original firmware behaviour).
//  - Note-off / poly aftertouch: go to the instance(s) that own the key.
//  - Channels in mono mode (CC126) or with portamento on (CC65) are pinned
//    to one instance, so legato/portamento work exactly as on the hardware.
//  - Rhythm parts: notes of GS exclusive groups (hi-hats etc.) are pinned to
//    one instance per channel so choke groups still work; all other drum
//    notes are load-balanced.
//  - Everything else (CC, program change, pitch bend, channel pressure,
//    SysEx, resets) is broadcast to all instances, so all instances always
//    share the same part/effect state.

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <span>
#include <functional>
#include <vector>

class PolyRouter {
public:
	// A routed message: target bitmask over the instances.
	using Mask = uint64_t;
	static constexpr int MaxInstances = 64;

	void SetCapacity(int partials_per_instance) { capacity = partials_per_instance; }
	// Packing: fill the lowest instance first (lets upper ones go idle and
	// sleep); otherwise spread evenly.
	void SetPacking(bool on) { packing = on; }
	// Called when a note fits into no awake instance; should wake one
	// instance (via SetAwake) and return true, or return false.
	void SetWakeHandler(std::function<bool()> fn) { wake_handler = std::move(fn); }

	void Init(int num_instances)
	{
		n = std::clamp(num_instances, 1, MaxInstances);
		all = (n == 64) ? ~Mask{0} : ((Mask{1} << n) - 1);
		load.assign(n, 0);
		pending.assign(n, 0);
		awake.assign(n, true);
		Reset();
	}

	void Reset()
	{
		for (auto& ch : owners) ch.fill(0);
		pinned.fill(-1);
		mono.fill(false);
		portamento.fill(false);
		drum_home.fill(-1);
		ResetGsParts();
		std::fill(pending.begin(), pending.end(), 0);
		rr = 0;
	}

	int NumInstances() const { return n; }

	// Upper limit of instances that may receive new notes (setup menu)
	void SetAllowed(int a) { allowed = std::clamp(a, 1, n); }
	int Allowed() const { return allowed; }
	bool IsRhythm(int ch) const { return IsRhythmChannel(ch); }

	// Dynamic instance management: sleeping instances get no notes.
	bool IsAwake(int i) const { return awake[i]; }
	void SetAwake(int i, bool on)
	{
		awake[i] = on;
		if (on) {
			return;
		}
		// Release pins that point at an instance that goes to sleep
		for (int ch = 0; ch < 16; ++ch) {
			if (drum_home[ch] == i) drum_home[ch] = -1;
			if (pinned[ch] == i) {
				pinned[ch] = -1;
				UpdatePin(ch);
			}
		}
	}
	// True if the router still considers a key held on this instance
	bool OwnsNotes(int i) const
	{
		const Mask bit = Mask{1} << i;
		for (const auto& ch : owners)
			for (const auto m : ch)
				if (m & bit) return true;
		return false;
	}
	int MeasuredLoad(int i) const { return load[i] + pending[i] * 2; }
	// Forget key ownership on an instance (it is verifiably silent): a held
	// key without sounding voice must not keep the instance awake.
	void ForgetInstance(int i)
	{
		const Mask bit = Mask{1} << i;
		for (auto& ch : owners)
			for (auto& m : ch) m &= ~bit;
	}

	// Called before routing each block with the measured number of sounding
	// partials per instance.
	void SetMeasuredLoad(int instance, int active_partials)
	{
		load[instance]    = active_partials;
		pending[instance] = 0;
	}

	// Short (1-3 byte) channel message. Returns target mask.
	Mask RouteShort(const uint8_t* d)
	{
		const uint8_t status = d[0] & 0xf0;
		const int ch         = d[0] & 0x0f;

		switch (status) {
		case 0x90:
			if (d[2] != 0) return NoteOn(ch, d[1] & 0x7f);
			[[fallthrough]];
		case 0x80: return NoteOff(ch, d[1] & 0x7f);

		case 0xa0: { // poly aftertouch → owner(s) only
			const Mask m = owners[ch][d[1] & 0x7f];
			return m ? m : all;
		}
		case 0xb0: ControlChange(ch, d[1], d[2]); return all;
		default: return all; // PC, channel pressure, pitch bend, system
		}
	}

	// SysEx is always broadcast; we only snoop it to track GS state.
	Mask RouteSysEx(std::span<const uint8_t> msg)
	{
		SnoopSysEx(msg);
		return all;
	}

private:
	int n    = 1;
	int capacity = 24;
	bool packing = false;
public:
	// Diagnostics: reasons why note-ons went to an instance
	// 0 = packing/pick, 1 = pinned (mono/portamento), 2 = drum group, 3 = re-trigger
	uint32_t reason_count[4][64] = {};
private:
	int allowed  = MaxInstances;
	std::function<bool()> wake_handler;
	Mask all = 1;
	std::vector<int> load, pending;
	std::vector<bool> awake;
	int rr = 0;

	std::array<std::array<Mask, 128>, 16> owners{};
	std::array<int, 16> pinned{};
	std::array<bool, 16> mono{}, portamento{};
	std::array<int, 16> drum_home{};

	// GS part state (part index 0..15 = part 1..16)
	std::array<int, 16> part_rx_ch{};
	std::array<bool, 16> part_rhythm{};

	void ResetGsParts()
	{
		for (int p = 0; p < 16; ++p) {
			part_rx_ch[p]  = p;
			part_rhythm[p] = (p == 9);
		}
	}

	bool IsRhythmChannel(int ch) const
	{
		for (int p = 0; p < 16; ++p)
			if (part_rhythm[p] && part_rx_ch[p] == ch) return true;
		return false;
	}

	static bool IsExclusiveGroupNote(int note)
	{
		// GS/GM drum-kit exclusive classes (hi-hat, whistle, guiro, cuica,
		// triangle, surdo) plus the SC-55 SFX/high-Q range around 27-29.
		switch (note) {
		case 27: case 28: case 29:
		case 42: case 44: case 46:
		case 71: case 72:
		case 73: case 74:
		case 78: case 79:
		case 80: case 81:
		case 86: case 87: return true;
		default: return false;
		}
	}

	int Pick()
	{
		if (packing) {
			for (;;) {
				for (int i = 0; i < n && i < allowed; ++i) {
					if (awake[i] && MeasuredLoad(i) <= capacity - 2) {
						++rr;
						return i;
					}
				}
				// Exact on-demand waking: only when this note fits nowhere
				if (!wake_handler || !wake_handler()) break;
			}
		}
		return LeastLoaded();
	}

	int LeastLoaded()
	{
		int best = -1, best_load = 1 << 30;
		for (int k = 0; k < n; ++k) {
			const int i = (rr + k) % n; // round-robin tie-break
			if (!awake[i] || i >= allowed) continue;
			// a fresh note-on typically allocates 1-2 partials
			const int l = load[i] + pending[i] * 2;
			if (l < best_load) {
				best_load = l;
				best      = i;
			}
		}
		if (best < 0) best = 0; // instance 0 is always awake
		rr = (best + 1) % n;
		return best;
	}

	Mask NoteOn(int ch, int note)
	{
		int target = -1;

		int why = 0;
		if (pinned[ch] >= 0) {
			target = pinned[ch];
			why    = 1;
		} else if (IsRhythmChannel(ch) && IsExclusiveGroupNote(note)) {
			if (drum_home[ch] < 0) drum_home[ch] = Pick();
			target = drum_home[ch];
			why    = 2;
		} else if (owners[ch][note] &&
		           MeasuredLoad(std::countr_zero(owners[ch][note])) <=
		                   capacity - 2) {
			// key still held: re-trigger on the instance that owns it,
			// as long as that instance has room (otherwise the firmware
			// would steal a voice there while other instances are free)
			target = std::countr_zero(owners[ch][note]);
			why    = 3;
		} else {
			target = Pick();
		}
		++reason_count[why][target];

		owners[ch][note] |= Mask{1} << target;
		++pending[target];
		return Mask{1} << target;
	}

	Mask NoteOff(int ch, int note)
	{
		const Mask m      = owners[ch][note];
		owners[ch][note] = 0;
		return m ? m : all; // unknown key: broadcast, harmless
	}

	void UpdatePin(int ch)
	{
		const bool want = mono[ch] || portamento[ch];
		if (want && pinned[ch] < 0) {
			pinned[ch] = Pick();
		} else if (!want) {
			pinned[ch] = -1;
		}
	}

	void ControlChange(int ch, int cc, int val)
	{
		switch (cc) {
		case 65: portamento[ch] = val >= 64; UpdatePin(ch); break;
		case 126: mono[ch] = true; UpdatePin(ch); break;  // Mono On
		case 127: mono[ch] = false; UpdatePin(ch); break; // Poly On
		case 120: // All Sound Off
		case 123: // All Notes Off
		case 124: case 125:
			owners[ch].fill(0);
			break;
		case 121: // Reset All Controllers
			portamento[ch] = false;
			UpdatePin(ch);
			break;
		}
	}

	void SnoopSysEx(std::span<const uint8_t> m)
	{
		// GM System On: F0 7E xx 09 01 F7
		if (m.size() >= 6 && m[0] == 0xf0 && m[1] == 0x7e && m[3] == 0x09) {
			Reset();
			return;
		}
		// Roland GS DT1: F0 41 dev 42 12 aa bb cc data... sum F7
		if (m.size() < 10 || m[0] != 0xf0 || m[1] != 0x41 || m[3] != 0x42 ||
		    m[4] != 0x12)
			return;

		const int a = m[5], b = m[6], c = m[7];
		const auto data = m.subspan(8, m.size() - 10); // minus checksum+F7

		if (a == 0x40 && b == 0x00 && c == 0x7f) { // GS Reset
			Reset();
			return;
		}
		if (a != 0x40 || (b & 0xf0) != 0x10) return;

		// block x: 0 → part 10, 1..9 → parts 1..9, A..F → parts 11..16
		const int x    = b & 0x0f;
		const int part = (x == 0) ? 9 : (x <= 9 ? x - 1 : x);

		for (size_t i = 0; i < data.size(); ++i) {
			const int addr = c + static_cast<int>(i);
			if (addr == 0x02) { // Rx channel (0x10 = off)
				part_rx_ch[part] = data[i] < 16 ? data[i] : -1;
			} else if (addr == 0x15) { // Use For Rhythm Part
				part_rhythm[part] = data[i] != 0;
			}
		}
	}
};
