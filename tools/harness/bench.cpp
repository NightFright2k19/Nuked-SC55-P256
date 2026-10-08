#include "nuked-sc55/backend/emu.h"
#include "nuked-sc55/common/rom_loader.h"
#include <chrono>
#include <cstdio>
#include <bit>
static long frames = 0;
static void cb(void*, const AudioFrame<int32_t>&) { ++frames; }
static int active(Emulator& e){int n=0; auto& p=e.GetPCM(); for(int s=0;s<p.config.reg_slots && s<28;s++) if(p.ram2[s][10]) n++; return n;}
static void run(Emulator& e, long n){ long t=frames+n; while(frames<t) MCU_Step(e.GetMCU()); }
int main(int argc,char**argv){
  const char* dir=argv[1]; const char* rs=argv[2];
  Emulator e; EMU_Options o{}; e.Init(o);
  common::LoadRomsetResult r{}; common::RomOverrides ov;
  auto err=common::LoadRomset(dir, rs, common::RomLoader::Hashing, ov, r);
  if(err!=common::LoadRomsetError{}){printf("load err %s\n",common::ToCString(err));return 1;}
  e.LoadRoms(r.romset,r.romset_info); e.SetSampleCallback(cb,nullptr);
  e.Reset(); e.PostSystemReset(EMU_SystemReset::GS_RESET);
  double fs=PCM_GetOutputFrequency(e.GetPCM());
  run(e,(long)fs*4);
  auto t0=std::chrono::steady_clock::now(); run(e,(long)fs*10);
  double s=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
  printf("%s: fs=%.0f Hz, 10 s Audio in %.2f s -> %.1f%% eines Kerns\n",rs,fs,s,s*10);
  // Poly-Test: Ch1 Piano, Sustain an, 40 Noten
  e.PostMIDI(0xB0);e.PostMIDI(64);e.PostMIDI(127);
  for(int n=0;n<40;n++){ e.PostMIDI(0x90);e.PostMIDI(36+n);e.PostMIDI(100); run(e,(long)(fs*0.05)); }
  printf("aktive Slots nach 40 Noten (Sustain): %d\n",active(e));
  e.PostMIDI(0xB0);e.PostMIDI(64);e.PostMIDI(0);
  for(int n=0;n<40;n++){e.PostMIDI(0x80);e.PostMIDI(36+n);e.PostMIDI(0);}
  for(int i=0;i<8;i++){ run(e,(long)(fs*0.5)); printf("  +%.1fs: %d\n",0.5*(i+1),active(e)); }
}
