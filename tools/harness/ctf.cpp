#include "nuked-sc55/backend/emu.h"
#include "nuked-sc55/common/rom_loader.h"
#include <cstdio>
#include <cstdlib>
static long frames=0; static double ss=0; static void cb(void*,const AudioFrame<int32_t>&f){++frames; double v=f.left/2147483648.0; ss+=v*v;}
static void run(Emulator& e,long n){long t=frames+n;while(frames<t)MCU_Step(e.GetMCU());}
int main(int,char**a){
  Emulator e;EMU_Options o{};e.Init(o);common::LoadRomsetResult r{};common::RomOverrides ov;
  if(common::LoadRomset(a[1],a[2],common::RomLoader::Hashing,ov,r)!=common::LoadRomsetError{}){puts("load fail");return 1;}
  e.LoadRoms(r.romset,r.romset_info);e.SetSampleCallback(cb,nullptr);e.Reset();e.PostSystemReset(EMU_SystemReset::GS_RESET);
  double fs=PCM_GetOutputFrequency(e.GetPCM()); run(e,fs*10);
  int prog=atoi(a[3]);
  printf("%-34s", a[2]);
  for(int bank:{0,1,2,3,5,9}){
    e.PostMIDI(0xB0);e.PostMIDI(0);e.PostMIDI(bank); e.PostMIDI(0xC0);e.PostMIDI(prog); run(e,fs*0.1);
    ss=0; long f0=frames; e.PostMIDI(0x90);e.PostMIDI(60);e.PostMIDI(110); run(e,fs*0.5); e.PostMIDI(0x80);e.PostMIDI(60);e.PostMIDI(0);
    double rms=sqrt(ss/(frames-f0)); run(e,fs*1.5);
    printf("  Bank %d: %s", bank, rms>0.003?"Ton ":"STILL");
  } puts("");
}
