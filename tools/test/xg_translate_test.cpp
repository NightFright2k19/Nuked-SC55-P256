// Unit test of the XG compatibility layer (src/src/xg_translate.h), no emulator needed:
//   g++ -std=c++20 -I src/src tools/test/xg_translate_test.cpp -o /tmp/xg_translate_test && /tmp/xg_translate_test
#include "xg_translate.h"

#include <cstdio>
#include <vector>

using namespace XgTranslate;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

static bool Is(const Msg& m, std::initializer_list<uint8_t> e)
{
	if (m.n != e.size()) return false;
	size_t i = 0;
	for (uint8_t v : e) if (m.b[i++] != v) return false;
	return true;
}

static void Sx(Translator& t, std::vector<uint8_t> m, Out& o, int port = 0) { t.SysEx(m, port, o); }
static void Cc(Translator& t, int ch, int cc, int v, Out& o, int port = 0)
{
	const uint8_t d[3] = {uint8_t(0xb0 | ch), uint8_t(cc), uint8_t(v)};
	t.Short(d, 3, port, o);
}
static void Pc(Translator& t, int ch, int p, Out& o, int port = 0)
{
	const uint8_t d[2] = {uint8_t(0xc0 | ch), uint8_t(p)};
	t.Short(d, 2, port, o);
}

int main()
{
	Translator t;
	Out o;

	// disabled: everything passes
	Cc(t, 0, 32, 5, o); CHECK(o.pass);
	Pc(t, 0, 10, o); CHECK(o.pass);

	// XG System On -> GS Reset, mode on
	Sx(t, {0xf0, 0x43, 0x10, 0x4c, 0x00, 0x00, 0x7e, 0x00, 0xf7}, o);
	CHECK(!o.pass && o.count == 1 && t.Enabled());
	CHECK(Is(o.m[0], {0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7f, 0x00, 0x41, 0xf7}));

	// the first channel message after it also carries the XG default effects (Hall 1 reverb, Chorus 1); CC 94 is dropped
	Cc(t, 0, 94, 127, o);
	CHECK(!o.pass && o.count == 4 && o.m[0].b[7] == 0x30 && o.m[0].b[8] == 4 && o.m[1].b[7] == 0x34 && o.m[1].b[8] == 48 &&
	      o.m[2].b[7] == 0x33 && o.m[2].b[8] == 96 && o.m[3].b[7] == 0x38 && o.m[3].b[8] == 0);
	Cc(t, 0, 94, 127, o); CHECK(!o.pass && o.count == 0);
	Cc(t, 0, 91, 40, o); CHECK(o.pass && o.count == 0);

	// melody voice: bank select held back, applied at the program change in GS form (CC32 is never passed on)
	Cc(t, 2, 0, 0, o); CHECK(!o.pass && o.count == 0);
	Cc(t, 2, 32, 16, o); CHECK(!o.pass && o.count == 0);
	Pc(t, 2, 24, o);
	CHECK(!o.pass && o.count == 3);
	CHECK(Is(o.m[0], {0xb2, 0, MelodyVoice(0, 16, 24).variation}) && Is(o.m[1], {0xb2, 32, 4}) && Is(o.m[2], {0xc2, 24}));

	// voice table: sorted, every entry found; an unlisted voice plays as the basic voice; SFX voices change program
	for (size_t i = 1; i < kVoiceTable.size(); ++i)
		CHECK(kVoiceTable[i - 1].lsb * 128 + kVoiceTable[i - 1].prog < kVoiceTable[i].lsb * 128 + kVoiceTable[i].prog);
	for (const auto& e : kVoiceTable) CHECK(MelodyVoice(0, e.lsb, e.prog).variation == e.variation && e.variation < 64);
	CHECK(MelodyVoice(0, 0, 24).variation == 0 && MelodyVoice(0, 0, 24).prog == 24);
	CHECK(MelodyVoice(0, 127, 5).variation == 0);
	CHECK(MelodyVoice(64, 0, 3).prog == kSfxTable[3][1] || kSfxTable[3][0] == 0xff);

	// other controllers pass
	Cc(t, 2, 7, 100, o); CHECK(o.pass);

	// drum kit on channel 10: Room (XG program 8) -> SC-8820 Room (8); part is a drum part already
	Cc(t, 9, 0, 127, o); Cc(t, 9, 32, 0, o); Pc(t, 9, 8, o);
	CHECK(o.count == 3 && Is(o.m[2], {0xc9, 8}));
	// channel 10 with Drum Part Protect: a melody bank is ignored, the program change picks a kit
	Cc(t, 9, 0, 0, o); Pc(t, 9, 16, o);
	CHECK(o.count == 3 && Is(o.m[2], {0xc9, 16}));
	// SFX kit
	Cc(t, 9, 0, 126, o); Pc(t, 9, 0, o);
	CHECK(Is(o.m[2], {0xc9, 56}));

	// drum kit on channel 11: part becomes a drum part (40 1A 15 02, checksum 0F), then a melody bank turns it back
	Cc(t, 10, 0, 127, o); Pc(t, 10, 25, o);
	CHECK(o.count == 4);
	CHECK(Is(o.m[0], {0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x1a, 0x15, 0x02, 0x0f, 0xf7}));
	CHECK(Is(o.m[3], {0xca, 25}));
	Cc(t, 10, 0, 0, o); Pc(t, 10, 5, o);
	CHECK(o.count == 4 && o.m[0].b[8] == 0x00 && Is(o.m[3], {0xca, 5}));

	// Drum Part Protect off: channel 10 may become a melody part
	Sx(t, {0xf0, 0x43, 0x10, 0x4c, 0x00, 0x00, 0x07, 0x00, 0xf7}, o); CHECK(!o.pass && o.count == 0);
	Cc(t, 9, 0, 0, o); Pc(t, 9, 3, o);
	CHECK(o.count == 4 && o.m[0].b[6] == 0x10 && o.m[0].b[8] == 0x00 && Is(o.m[3], {0xc9, 3}));

	// Master Volume -> GS 40 00 04 (checksum: 128 - (0x40 + 0x04 + 0x7f) % 128 = 0x3d)
	Sx(t, {0xf0, 0x43, 0x10, 0x4c, 0x00, 0x00, 0x04, 0x7f, 0xf7}, o);
	CHECK(o.count == 1 && Is(o.m[0], {0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x04, 0x7f, 0x3d, 0xf7}));

	// other XG SysEx is dropped, non-XG SysEx passes
	Sx(t, {0xf0, 0x43, 0x10, 0x4c, 0x02, 0x01, 0x00, 0x05, 0x00, 0xf7}, o); CHECK(!o.pass && o.count == 0);
	Sx(t, {0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x01, 0x30, 0x03, 0x0c, 0xf7}, o); CHECK(o.pass && t.Enabled());

	// effects: reverb / chorus type and return, part sends
	Sx(t, {0xf0, 0x43, 0x10, 0x4c, 0x02, 0x01, 0x00, 0x01, 0x01, 0xf7}, o); // Hall 2: GS Hall 1, time 80, level 48
	CHECK(o.count == 3 && Is(o.m[0], {0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x01, 0x30, 0x03, 0x0c, 0xf7}) &&
	      o.m[1].b[7] == 0x34 && o.m[1].b[8] == 80 && o.m[2].b[7] == 0x33 && o.m[2].b[8] == 48);
	Sx(t, {0xf0, 0x43, 0x10, 0x4c, 0x02, 0x01, 0x00, 0x04, 0x00, 0xf7}, o); CHECK(o.count == 3 && o.m[0].b[8] == 5); // Plate
	Sx(t, {0xf0, 0x43, 0x10, 0x4c, 0x02, 0x01, 0x20, 0x41, 0x02, 0xf7}, o); // Chorus 3 -> macro 2
	CHECK(o.count == 1 && o.m[0].b[6] == 0x01 && o.m[0].b[7] == 0x38 && o.m[0].b[8] == 2);
	Sx(t, {0xf0, 0x43, 0x10, 0x4c, 0x02, 0x01, 0x20, 0x43, 0x00, 0xf7}, o); CHECK(o.count == 1 && o.m[0].b[8] == 5); // Flanger
	Sx(t, {0xf0, 0x43, 0x10, 0x4c, 0x02, 0x01, 0x0c, 0x50, 0xf7}, o); CHECK(o.count == 1 && o.m[0].b[7] == 0x33 && o.m[0].b[8] == 0x50);
	Sx(t, {0xf0, 0x43, 0x10, 0x4c, 0x02, 0x01, 0x2c, 0x30, 0xf7}, o); CHECK(o.count == 1 && o.m[0].b[7] == 0x3a && o.m[0].b[8] == 0x30);
	Sx(t, {0xf0, 0x43, 0x10, 0x4c, 0x08, 0x03, 0x13, 0x45, 0xf7}, o); CHECK(!o.pass && o.count == 1 && Is(o.m[0], {0xb3, 91, 0x45}));
	Sx(t, {0xf0, 0x43, 0x10, 0x4c, 0x08, 0x0a, 0x12, 0x20, 0xf7}, o); CHECK(o.count == 1 && Is(o.m[0], {0xba, 93, 0x20}));

	// second MIDI input (port B): own part state
	Cc(t, 0, 0, 127, o, 1); Pc(t, 0, 0, o, 1);
	CHECK(o.count == 4 && o.m[0].b[6] == 0x11);

	// GM On / GM2 On / GS Reset end XG mode
	Sx(t, {0xf0, 0x7e, 0x7f, 0x09, 0x03, 0xf7}, o); CHECK(o.pass && !t.Enabled());
	Sx(t, {0xf0, 0x43, 0x10, 0x4c, 0x00, 0x00, 0x7e, 0x00, 0xf7}, o); CHECK(t.Enabled());
	Sx(t, {0xf0, 0x41, 0x10, 0x42, 0x12, 0x40, 0x00, 0x7f, 0x00, 0x41, 0xf7}, o); CHECK(o.pass && !t.Enabled());
	Sx(t, {0xf0, 0x43, 0x10, 0x4c, 0x00, 0x00, 0x7e, 0x00, 0xf7}, o);
	Sx(t, {0xf0, 0x7e, 0x7f, 0x09, 0x01, 0xf7}, o); CHECK(!t.Enabled());

	std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
	return failures != 0;
}
