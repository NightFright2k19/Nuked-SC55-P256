// Issue #120: XG System On counts as a reset in the state log of the SC-88 Pro (its firmware answers it with the
// XG fallback mode and initialises), but not on the other models. The model sets this at run time
// (NukedSc55::XgOnResets, issue #142); both settings are checked here.
//   g++ -std=c++20 -DNUKED_SC55_ENGINE_88PRO -I src/src tools/test/xg_reset_88pro_test.cpp -o /tmp/t && /tmp/t
#include "state_log.h"

#include <cstdio>
#include <vector>

int main()
{
	const std::vector<uint8_t> xg_on = {0xf0, 0x43, 0x10, 0x4c, 0x00, 0x00, 0x7e, 0x00, 0xf7};
	const std::vector<uint8_t> xg_on_dev3 = {0xf0, 0x43, 0x13, 0x4c, 0x00, 0x00, 0x7e, 0x00, 0xf7};
	const std::vector<uint8_t> xg_vol = {0xf0, 0x43, 0x10, 0x4c, 0x00, 0x00, 0x04, 0x7f, 0xf7};
	const std::vector<uint8_t> gs = {0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7f, 0x00, 0x41, 0xf7};
	int failures = 0;
	for (const bool sc88pro : {true, false}) {
		failures += StateLog::IsReset(xg_on, sc88pro) != sc88pro;
		failures += StateLog::IsReset(xg_on_dev3, sc88pro) != sc88pro;
		failures += StateLog::IsReset(xg_vol, sc88pro) != false; // other XG messages are no reset
		failures += StateLog::IsReset(gs, sc88pro) != true;
		StateLog log; // the log itself follows SetXgOnResets
		log.SetXgOnResets(sc88pro);
		log.AddSysEx(gs);
		log.AddSysEx(xg_vol);
		log.AddSysEx(xg_on);
		std::vector<StateLog::Entry> e;
		log.CopySince(0, e);
		failures += (e.size() == 1) != sc88pro; // SC-88 Pro: only the XG System On is left
	}
	std::printf("%s (%d failures)\n", failures ? "FAIL" : "ok", failures);
	return failures != 0;
}
