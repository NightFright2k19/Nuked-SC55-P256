#include <windows.h>
#include "vst2/vst2_abi.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace vst2;
static intptr_t cb(AEffect*,int32_t op,int32_t,intptr_t,void*,float){ return op==audioMasterVersion?2400:0; }
int main(int,char**a){
  HMODULE h=LoadLibraryA(a[1]); if(!h){printf("LoadLibrary fail %lu\n",GetLastError());return 1;}
  auto mainf=(AEffect*(*)(HostCallback))GetProcAddress(h,"VSTPluginMain");
  AEffect* fx=mainf(cb); if(!fx||fx->magic!=kMagic){puts("VSTPluginMain fail (ROMs?)");return 1;}
  char name[64]={}; fx->dispatcher(fx,effGetEffectName,0,0,name,0);
  printf("Name: %s | ID %08x | Synth %d | Outputs %d | Kategorie %d\n",name,fx->uniqueID,!!(fx->flags&FlagIsSynth),fx->numOutputs,(int)fx->dispatcher(fx,effGetPlugCategory,0,0,0,0));
  fx->dispatcher(fx,effOpen,0,0,0,0); fx->dispatcher(fx,effSetSampleRate,0,0,0,48000.f); fx->dispatcher(fx,effSetBlockSize,0,512,0,0);
  auto t0=std::chrono::steady_clock::now(); fx->dispatcher(fx,effMainsChanged,0,1,0,0);
  printf("Boot: %.2f s\n",std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count());
  int mode=atoi(a[2]);
  std::vector<float> L(1024),R(1024); float* out[2]={L.data(),R.data()};
  double ss=0; long n=0, total=0; double tp=0;
  for(int b=0;b<480;b++){
    int frames=(b%7==3)?1024:512; // gelegentlich größere Blöcke -> Chunking
    std::vector<MidiEvent> me; std::vector<uint8_t> gs={0xF0,0x41,0x10,0x42,0x12,0x40,0x01,0x30,0x04,0x0B,0xF7}; SysExEvent sx{};
    auto add=[&](int d,uint8_t x,uint8_t y,uint8_t z){MidiEvent m{};m.type=kMidiType;m.byteSize=sizeof m;m.deltaFrames=d;m.midiData[0]=x;m.midiData[1]=y;m.midiData[2]=z;me.push_back(m);};
    bool sysex=false;
    if(b==0){add(0,0xC0,0,0);add(0,0xC1,48,0);add(0,0xB0,64,127);add(0,0xB1,64,127); sysex=true;} // + GS Reverb-Macro SysEx
    if(mode==1) for(int k=0;k<256;k++){ int bb=10+k; if(bb==b) add(k%frames,0x90|(k&1),24+(k*7)%80,90); }
    if(mode==2){ if(b==10){add(0,0xB2,0,1);add(1,0xC2,16,0);} if(b==20) add(0,0x92,60,110); if(b==200) add(0,0x82,60,0);}
    std::vector<Event*> ptrs; for(auto&m:me) ptrs.push_back((Event*)&m);
    if(sysex){sx.type=kSysExType;sx.byteSize=sizeof sx;sx.dumpBytes=gs.size();sx.sysexDump=gs.data();ptrs.push_back((Event*)&sx);}
    std::vector<char> buf(sizeof(Events)+ptrs.size()*sizeof(void*)); auto ev=(Events*)buf.data(); ev->numEvents=ptrs.size(); for(size_t i=0;i<ptrs.size();i++) ev->events[i]=ptrs[i];
    fx->dispatcher(fx,effProcessEvents,0,0,ev,0);
    auto t=std::chrono::steady_clock::now(); fx->processReplacing(fx,nullptr,out,frames); tp+=std::chrono::duration<double>(std::chrono::steady_clock::now()-t).count();
    if(total>=48000*3){for(int i=0;i<frames;i++)ss+=L[i]*L[i]; n+=frames;}
    if(mode==2 && b>=20 && b<200){for(int i=0;i<frames;i++)ss+=L[i]*L[i]; n+=frames;}
    total+=frames;
  }
  printf("%s RMS: %.4f | Rechenzeit %.1f s fuer %.1f s Audio\n",mode==1?"256-Noten-Test":"CTF-Test (Bank 1, Prog 17)",sqrt(ss/n),tp,total/48000.0);
  fx->dispatcher(fx,effMainsChanged,0,0,0,0); fx->dispatcher(fx,effClose,0,0,0,0); return 0;
}
