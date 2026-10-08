#include "nuked-sc55/backend/emu.h"
#include "nuked-sc55/common/rom_loader.h"
#include <cstdio>
#include <cstdlib>
static long frames=0; static long peak=0; static void cb(void*,const AudioFrame<int32_t>& f){++frames; long a=labs((long)f.left); if(a>peak)peak=a;}
static void run(Emulator& e,long n){long t=frames+n;while(frames<t)MCU_Step(e.GetMCU());}
int main(int,char**argv){
  Emulator e;EMU_Options o{};e.Init(o);common::LoadRomsetResult r{};common::RomOverrides ov;
  common::LoadRomset(argv[1],argv[2],common::RomLoader::Hashing,ov,r);e.LoadRoms(r.romset,r.romset_info);
  e.SetSampleCallback(cb,nullptr);e.Reset();e.PostSystemReset(EMU_SystemReset::GS_RESET);
  double fs=PCM_GetOutputFrequency(e.GetPCM()); run(e,fs*4); peak=0;
  auto snap=[&](uint32_t a1[32][8],uint16_t a2[32][16]){auto&p=e.GetPCM();for(int s=0;s<32;s++){for(int i=0;i<8;i++)a1[s][i]=p.ram1[s][i];for(int i=0;i<16;i++)a2[s][i]=p.ram2[s][i];}};
  static uint32_t A1[3][32][8]; static uint16_t A2[3][32][16];
  snap(A1[0],A2[0]);
  // 3 Noten Piano (je 1-2 Partials), gehalten
  for(int n:{60,64,67}){e.PostMIDI(0x90);e.PostMIDI(n);e.PostMIDI(110);} run(e,fs*0.3); snap(A1[1],A2[1]); printf("peak waehrend Noten: %ld\n",peak);
  for(int n:{60,64,67}){e.PostMIDI(0x80);e.PostMIDI(n);e.PostMIDI(0);} run(e,fs*6); snap(A1[2],A2[2]);
  // Felder finden: idle==0/klein, aktiv auf wenigen Slots !=, nach Release zurück
  for(int i=0;i<16;i++){int chg=0,back=0;for(int s=0;s<28;s++){if(A2[1][s][i]!=A2[0][s][i]){chg++; if(A2[2][s][i]==A2[0][s][i])back++;}}
    if(chg) printf("ram2[*][%d]: %d Slots geändert, %d zurück auf Ruhewert\n",i,chg,back);}
  for(int i=0;i<8;i++){int chg=0,back=0;for(int s=0;s<28;s++){if(A1[1][s][i]!=A1[0][s][i]){chg++; if(A1[2][s][i]==A1[0][s][i])back++;}}
    if(chg) printf("ram1[*][%d]: %d Slots geändert, %d zurück\n",i,chg,back);}
  for(int s=0;s<24;s++) printf("slot %2d [7]%04x/%04x/%04x  [9] %5u/%5u/%5u  [10] %5u/%5u/%5u\n",s,A2[0][s][7],A2[1][s][7],A2[2][s][7],A2[0][s][9],A2[1][s][9],A2[2][s][9],A2[0][s][10],A2[1][s][10],A2[2][s][10]);
}
