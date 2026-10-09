#pragma once
// Nuked SC-55 Poly – compact MIDI state log
//
// Records every state-changing message that is broadcast to all instances
// (CC, program change, pitch bend, channel pressure, RPN/NRPN, SysEx).
// Only the LAST message per target parameter is kept, and entries are ordered
// by their last occurrence. Replaying all entries newer than a given sequence
// number therefore brings a sleeping instance to the same part/effect state
// as the running ones, with a few hundred bytes instead of a full history.
//
// Ordering by last occurrence keeps the semantics of resets too: e.g.
// "CC1=100, Reset All Controllers" replays in that order, while
// "Reset All Controllers, CC1=100" keeps CC1 after the reset.
//
// Devices with two MIDI inputs (SC-88 Pro): every entry remembers its input
// port and is replayed on it; channel state is kept per port.

#include <array>
#include <cstdint>
#include <span>
#include <vector>

class StateLog {
public:
	struct Entry {
		uint64_t key;
		uint64_t seq;
		bool is_reset; // GS reset / GM on: needs settle time after replay
		std::vector<uint8_t> bytes;
		uint8_t port = 0; // MIDI input to replay on
	};

	void Clear()
	{
		entries.clear();
		bank_msb.fill(0);
		bank_lsb.fill(0);
		rpn_msb.fill(0x7f);
		rpn_lsb.fill(0x7f);
		rpn_is_nrpn.fill(false);
	}

	uint64_t Seq() const { return seq; }

	// Copy of all entries newer than `since` (for the background warmer)
	void CopySince(uint64_t since, std::vector<Entry>& out) const
	{
		out.clear();
		for (const auto& e : entries)
			if (e.seq > since) out.push_back(e);
	}

	// Entries newer than `since`, oldest first
	template <typename F>
	void ForEachSince(uint64_t since, F&& f) const
	{
		for (const auto& e : entries) {
			if (e.seq > since) f(e);
		}
	}

	void AddShort(const uint8_t* d, int port = 0)
	{
		const uint8_t status = d[0] & 0xf0;
		port                 = port & 3;
		const int ch         = d[0] & 0x0f;
		const int lch        = port * 16 + ch; // logical channel: state slot
		cur_port             = uint8_t(port);

		switch (status) {
		case 0xb0: AddCC(lch, d[1] & 0x7f, d[2] & 0x7f); break;
		case 0xc0: { // store with the bank that was valid at that time
			const uint8_t b[] = {uint8_t(0xb0 | ch), 0, bank_msb[lch],
			                     uint8_t(0xb0 | ch), 32, bank_lsb[lch],
			                     d[0], uint8_t(d[1] & 0x7f)};
			Put(Key(2, lch, 0), b);
		} break;
		case 0xd0: {
			const uint8_t b[] = {d[0], uint8_t(d[1] & 0x7f)};
			Put(Key(3, lch, 0), b);
		} break;
		case 0xe0: {
			const uint8_t b[] = {d[0], uint8_t(d[1] & 0x7f), uint8_t(d[2] & 0x7f)};
			Put(Key(4, lch, 0), b);
		} break;
		default: break; // notes, poly AT, realtime: not state
		}
	}

	void AddSysEx(std::span<const uint8_t> m, int port = 0)
	{
		if (m.size() < 2) return;
		cur_port = uint8_t(port & 3);

		const bool gm_on = m.size() >= 6 && m[1] == 0x7e && m[3] == 0x09;
		const bool roland_dt1 = m.size() >= 10 && m[1] == 0x41 &&
		                        m[3] == 0x42 && m[4] == 0x12;
		const bool gs_reset = roland_dt1 && m[5] == 0x40 && m[6] == 0x00 &&
		                      m[7] == 0x7f;

		if (gm_on || gs_reset) {
			// Everything before a reset is irrelevant
			Clear();
			Put(Key(6, 0, 0), m, true);
			return;
		}
		if (roland_dt1) {
			// last write per (port, address, length) wins
			const uint64_t addr = (uint64_t(m[5]) << 16) | (uint64_t(m[6]) << 8) | m[7];
			Put(Key(7, cur_port, (addr << 16) | (m.size() & 0xffff)), m);
			return;
		}
		Put(Key(8, cur_port, Hash(m)), m);
	}

private:
	std::vector<Entry> entries;
	uint64_t seq = 0;
	uint8_t cur_port = 0; // port of the message being added
	// per logical channel (port * 16 + MIDI channel)
	std::array<uint8_t, 64> bank_msb{}, bank_lsb{};
	std::array<uint8_t, 64> rpn_msb{}, rpn_lsb{};
	std::array<bool, 64> rpn_is_nrpn{};

	static uint64_t Key(uint64_t type, uint64_t ch, uint64_t param)
	{
		return (type << 56) | (ch << 48) | (param & 0xffffffffffffull);
	}

	static uint64_t Hash(std::span<const uint8_t> m)
	{
		uint64_t h = 1469598103934665603ull;
		for (auto b : m) { h ^= b; h *= 1099511628211ull; }
		return h;
	}

	void Put(uint64_t key, std::span<const uint8_t> bytes, bool is_reset = false)
	{
		for (auto it = entries.begin(); it != entries.end(); ++it) {
			if (it->key == key) {
				entries.erase(it);
				break;
			}
		}
		entries.push_back({key, ++seq, is_reset, {bytes.begin(), bytes.end()}, cur_port});
	}

	void AddCC(int ch, int cc, int val) // ch = logical channel
	{
		const uint8_t st = uint8_t(0xb0 | (ch & 0x0f));
		switch (cc) {
		case 0: bank_msb[ch] = uint8_t(val); break;
		case 32: bank_lsb[ch] = uint8_t(val); break;
		case 99: rpn_is_nrpn[ch] = true; rpn_msb[ch] = uint8_t(val); return;
		case 98: rpn_is_nrpn[ch] = true; rpn_lsb[ch] = uint8_t(val); return;
		case 101: rpn_is_nrpn[ch] = false; rpn_msb[ch] = uint8_t(val); return;
		case 100: rpn_is_nrpn[ch] = false; rpn_lsb[ch] = uint8_t(val); return;
		case 6:
		case 38: {
			if (rpn_msb[ch] == 0x7f && rpn_lsb[ch] == 0x7f) return; // RPN null
			const uint8_t sel_m = rpn_is_nrpn[ch] ? 99 : 101;
			const uint8_t sel_l = rpn_is_nrpn[ch] ? 98 : 100;
			const uint8_t b[] = {st, sel_m, rpn_msb[ch], st, sel_l, rpn_lsb[ch],
			                     st, uint8_t(cc), uint8_t(val)};
			Put(Key(5, ch, (uint64_t(rpn_is_nrpn[ch]) << 24) |
			                       (uint64_t(rpn_msb[ch]) << 16) |
			                       (uint64_t(rpn_lsb[ch]) << 8) | uint64_t(cc)),
			    b);
			return;
		}
		case 96: case 97: return;          // inc/dec: not replayable
		case 120: case 123: case 124:
		case 125: return;                  // sound/notes off: no state
		case 126: case 127: {               // mono/poly share one slot
			const uint8_t b[] = {st, uint8_t(cc), uint8_t(val)};
			Put(Key(1, ch, 126), b);
			return;
		}
		}
		const uint8_t b[] = {st, uint8_t(cc), uint8_t(val)};
		Put(Key(1, ch, uint64_t(cc)), b);
	}
};
