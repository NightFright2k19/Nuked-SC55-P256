#include "nuked-sc55/backend/emu.h"
#include "nuked-sc55/common/rom_loader.h"
#include <cstdio>
#include <cstdlib>
static long frames=0; static void cb(void*,const AudioFrame<int32_t>&){++frames;}
static void run(Emulator& e,long n){long t=frames+n;while(frames<t)MCU_Step(e.GetMCU());}
static int act(Emulator&e){int n=0;auto&p=e.GetPCM();for(int s=0;s<28;s++) if(((p.voice_mask&p.voice_mask_pending)>>s&1)&&p.ram2[s][10])n++;return n;}
int main(int,char**a){
  Emulator e;EMU_Options o{};e.Init(o);common::LoadRomsetResult r{};common::RomOverrides ov;
  common::LoadRomset(a[1],a[2],common::RomLoader::Hashing,ov,r);e.LoadRoms(r.romset,r.romset_info);
  e.SetSampleCallback(cb,nullptr);e.Reset();e.PostSystemReset(EMU_SystemReset::GS_RESET);
  double fs=PCM_GetOutputFrequency(e.GetPCM()); run(e,fs*10);
  for(int prog:{16,17,19,20,48,49,52,80,73,61,88,89}){
    e.PostMIDI(0xC0);e.PostMIDI(prog); run(e,fs*0.1);
    e.PostMIDI(0x90);e.PostMIDI(60);e.PostMIDI(100); run(e,fs*1.0); int a1=act(e);
    run(e,fs*3.0); int a2=act(e);
    e.PostMIDI(0x80);e.PostMIDI(60);e.PostMIDI(0); run(e,fs*3);
    printf("Prog %3d: %d Partials nach 1 s, %d nach 4 s\n",prog+1,a1,a2);
  }
}
