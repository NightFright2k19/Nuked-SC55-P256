// Laedt ein VST2-Plugin, aktiviert es, spielt kurz und meldet den Speicherbedarf des Prozesses.
#include <windows.h>
#include <psapi.h>
#include "vst2/vst2_abi.h"
#include <cstdio>
#include <vector>
using namespace vst2;
static intptr_t cb(AEffect*,int32_t op,int32_t,intptr_t,void*,float){ return op==audioMasterVersion?2400:0; }
static void mem(const char* l){ PROCESS_MEMORY_COUNTERS_EX m{}; m.cb=sizeof m; GetProcessMemoryInfo(GetCurrentProcess(),(PROCESS_MEMORY_COUNTERS*)&m,sizeof m);
  printf("  %-22s Arbeitsspeicher %4zu MB | privat %4zu MB\n",l,m.WorkingSetSize>>20,m.PrivateUsage>>20); }
int main(int,char**a){ mem("vor dem Laden");
  HMODULE h=LoadLibraryA(a[1]); auto f=(AEffect*(*)(HostCallback))GetProcAddress(h,"VSTPluginMain"); AEffect* fx=f(cb);
  fx->dispatcher(fx,effOpen,0,0,0,0); fx->dispatcher(fx,effSetSampleRate,0,0,0,44100.f); fx->dispatcher(fx,effSetBlockSize,0,512,0,0);
  mem("nach dem Laden"); fx->dispatcher(fx,effMainsChanged,0,1,0,0);
  std::vector<float> L(512),R(512); float* o[2]={L.data(),R.data()}; for(int b=0;b<200;b++) fx->processReplacing(fx,nullptr,o,512);
  mem("aktiv"); return 0; }
