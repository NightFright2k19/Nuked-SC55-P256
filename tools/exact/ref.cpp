// Rendert eine Eventliste mit EINEM Emulator und gibt Hash + Rechenzeit aus.
#include "backend/emu.h"
#include "common/rom_loader.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>
static uint64_t h=1469598103934665603ull, frames=0;
static void cb(void*,const AudioFrame<int32_t>&f){ ++frames; for(int32_t v:{f.left,f.right}){ for(int k=0;k<4;k++){ h^=(uint8_t)(v>>(8*k)); h*=1099511628211ull; } } }
int main(int,char**a){
  struct E{double t; std::vector<uint8_t> d;}; std::vector<E> ev; std::ifstream f(a[3]); std::string line;
  while(std::getline(f,line)){std::istringstream is(line); E e; int n; is>>e.t>>n; for(int i=0;i<n;i++){int b;is>>b;e.d.push_back(b);} ev.push_back(e);}
  Emulator e; EMU_Options o{}; e.Init(o); common::LoadRomsetResult r{}; common::RomOverrides ov;
  if(common::LoadRomset(a[1],a[2],common::RomLoader::Hashing,ov,r)!=common::LoadRomsetError{}){puts("load");return 1;}
  e.LoadRoms(r.romset,r.romset_info); e.SetSampleCallback(cb,nullptr); e.Reset(); e.GetPCM().enable_oversampling = getenv("OVS") ? true : false; e.PostSystemReset(EMU_SystemReset::GS_RESET);
  double fs=PCM_GetOutputFrequency(e.GetPCM()); double dur=atof(a[4]); const double boot=10.0;
  auto t0=std::chrono::steady_clock::now(); size_t ei=0;
  while(frames < (boot+dur)*fs){
    while(ei<ev.size() && (ev[ei].t+boot)*fs <= frames){ e.PostMIDI(std::span{ev[ei].d.data(),ev[ei].d.size()}); ei++; }
    MCU_Step(e.GetMCU());
  }
  double s=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
  printf("%-14s hash %016llx  frames %llu  %.2f s\n",a[5],(unsigned long long)h,(unsigned long long)frames,s);
}
