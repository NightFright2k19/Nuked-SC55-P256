// Issue #120: XG System On counts as a reset in the state log of the SC-88 Pro (its firmware answers it with the
// XG fallback mode and initialises), but not on the other models.
//   g++ -std=c++20 -DNUKED_SC55_ENGINE_88PRO -DNUKED_SC55_DEVICE_88PRO -I src/src tools/test/xg_reset_88pro_test.cpp -o /tmp/t && /tmp/t
//   (with -DNUKED_SC55_DEVICE_8850 instead of -DNUKED_SC55_DEVICE_88PRO the XG System On is no reset)
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
#ifdef NUKED_SC55_DEVICE_88PRO
	const bool want = true;
#else
	const bool want = false;
#endif
	failures += StateLog::IsReset(xg_on) != want;
	failures += StateLog::IsReset(xg_on_dev3) != want;
	failures += StateLog::IsReset(xg_vol) != false; // other XG messages are no reset
	failures += StateLog::IsReset(gs) != true;
	std::printf("%s (%d failures)\n", failures ? "FAIL" : "ok", failures);
	return failures != 0;
}
