#pragma once
// Minimal, independently written description of the VST 2.4 binary
// interface (struct layouts, opcode numbers). Contains no Steinberg SDK code.
// "VST" is a trademark of Steinberg Media Technologies GmbH.

#include <cstdint>

namespace vst2 {

struct AEffect;

using HostCallback = intptr_t (*)(AEffect*, int32_t opcode, int32_t index,
                                  intptr_t value, void* ptr, float opt);
using DispatcherProc = intptr_t (*)(AEffect*, int32_t opcode, int32_t index,
                                    intptr_t value, void* ptr, float opt);
using ProcessProc       = void (*)(AEffect*, float** in, float** out, int32_t n);
using ProcessDoubleProc = void (*)(AEffect*, double** in, double** out, int32_t n);
using SetParamProc      = void (*)(AEffect*, int32_t index, float value);
using GetParamProc      = float (*)(AEffect*, int32_t index);

constexpr int32_t kMagic = 0x56737450; // 'VstP'

struct AEffect {
	int32_t magic;
	DispatcherProc dispatcher;
	ProcessProc process_deprecated;
	SetParamProc setParameter;
	GetParamProc getParameter;
	int32_t numPrograms;
	int32_t numParams;
	int32_t numInputs;
	int32_t numOutputs;
	int32_t flags;
	intptr_t reserved1;
	intptr_t reserved2;
	int32_t initialDelay;
	int32_t realQualities;
	int32_t offQualities;
	float ioRatio;
	void* object;
	void* user;
	int32_t uniqueID;
	int32_t version;
	ProcessProc processReplacing;
	ProcessDoubleProc processDoubleReplacing;
	char future[56];
};

// AEffect::flags
constexpr int32_t FlagHasEditor     = 1 << 0;
constexpr int32_t FlagCanReplacing  = 1 << 4;
constexpr int32_t FlagProgramChunks = 1 << 5;
constexpr int32_t FlagIsSynth       = 1 << 8;
constexpr int32_t FlagNoSoundInStop = 1 << 9;

// Plugin dispatcher opcodes
enum : int32_t {
	effOpen                 = 0,
	effClose                = 1,
	effSetProgram           = 2,
	effGetProgram           = 3,
	effSetProgramName       = 4,
	effGetProgramName       = 5,
	effGetParamLabel        = 6,
	effGetParamDisplay      = 7,
	effGetParamName         = 8,
	effSetSampleRate        = 10,
	effSetBlockSize         = 11,
	effMainsChanged         = 12,
	effEditGetRect          = 13,
	effEditOpen             = 14,
	effEditClose            = 15,
	effEditIdle             = 19,
	effGetChunk             = 23,
	effSetChunk             = 24,
	effProcessEvents        = 25,
	effGetProgramNameIndexed = 29,
	effGetOutputProperties  = 34,
	effGetPlugCategory      = 35,
	effGetEffectName        = 45,
	effGetVendorString      = 47,
	effGetProductString     = 48,
	effGetVendorVersion     = 49,
	effCanDo                = 51,
	effGetTailSize          = 52,
	effGetVstVersion        = 58,
	effStartProcess         = 71,
	effStopProcess          = 72,
	effGetNumMidiInputChannels = 78,
};

// Host callback opcodes
enum : int32_t {
	audioMasterVersion  = 1,
	audioMasterWantMidi = 6,
};

constexpr int32_t kPlugCategSynth = 2;

// Events
constexpr int32_t kMidiType  = 1;
constexpr int32_t kSysExType = 6;

struct Event {
	int32_t type;
	int32_t byteSize;
	int32_t deltaFrames;
	int32_t flags;
	char data[16];
};

struct MidiEvent {
	int32_t type;
	int32_t byteSize;
	int32_t deltaFrames;
	int32_t flags;
	int32_t noteLength;
	int32_t noteOffset;
	uint8_t midiData[4];
	int8_t detune;
	uint8_t noteOffVelocity;
	uint8_t reserved1;
	uint8_t reserved2;
};

struct SysExEvent {
	int32_t type;
	int32_t byteSize;
	int32_t deltaFrames;
	int32_t flags;
	int32_t dumpBytes;
	intptr_t resvd1;
	uint8_t* sysexDump;
	intptr_t resvd2;
};

struct ERect {
	int16_t top, left, bottom, right;
};

struct Events {
	int32_t numEvents;
	intptr_t reserved;
	Event* events[2]; // actually numEvents entries
};

constexpr int32_t kVstVersion = 2400;

} // namespace vst2
