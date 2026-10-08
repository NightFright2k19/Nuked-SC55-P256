#include <windows.h>
#include "clap/clap.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
struct Ev { clap_event_midi_t m; };
static std::vector<Ev> evs;
static uint32_t ev_size(const clap_input_events_t*){return evs.size();}
static const clap_event_header_t* ev_get(const clap_input_events_t*,uint32_t i){return &evs[i].m.header;}
static void midi(uint32_t t,uint8_t a,uint8_t b,uint8_t c){Ev e{};e.m.header={sizeof(clap_event_midi_t),t,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI,0};e.m.data[0]=a;e.m.data[1]=b;e.m.data[2]=c;evs.push_back(e);}
int main(int argc,char**argv){
  HMODULE h=LoadLibraryA(argv[1]); if(!h){printf("LoadLibrary fail %lu\n",GetLastError());return 1;}
  auto entry=(const clap_plugin_entry_t*)GetProcAddress(h,"clap_entry");
  entry->init(argv[1]);
  auto f=(const clap_plugin_factory_t*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
  printf("Plugins: %u\n",f->get_plugin_count(f));
  for(unsigned i=0;i<f->get_plugin_count(f);i++) printf("  %s | %s\n",f->get_plugin_descriptor(f,i)->id,f->get_plugin_descriptor(f,i)->name);
  clap_host_t host{CLAP_VERSION_INIT,nullptr,"t","t","","1",[](const clap_host_t*,const char*)->const void*{return nullptr;},
    [](const clap_host_t*){},[](const clap_host_t*){},[](const clap_host_t*){}};
  auto p=f->create_plugin(f,&host,argv[2]); if(!p||!p->init(p)){puts("init fail");return 1;}
  const uint32_t B=256; if(!p->activate(p,48000,1,B)){puts("activate fail");return 1;} p->start_processing(p);
  std::vector<float> L(B),R(B); float* ch[2]={L.data(),R.data()};
  clap_audio_buffer_t out{}; out.data32=ch; out.channel_count=2;
  clap_input_events_t in{nullptr,ev_size,ev_get};
  clap_process_t pr{}; pr.frames_count=B; pr.audio_outputs=&out; pr.audio_outputs_count=1; pr.in_events=&in;
  long nb=(long)(5*48000/B); double ss_late=0; long nlate=0; double tp=0;
  for(long b=0;b<nb;b++){ evs.clear();
    if(b==0){midi(0,0xC0,0,0);midi(1,0xC1,48,0);midi(2,0xB0,64,127);midi(3,0xB1,64,127);}
    for(int k=0;k<256;k++){ long t=10+(long)(k*0.01*48000/B); if(t==b) midi(k%B,0x90|(k&1),(uint8_t)(24+(k*7)%80),90);}
    auto t0=std::chrono::steady_clock::now(); p->process(p,&pr); tp+=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
    if(b*B>=4*48000){ for(uint32_t i=0;i<B;i++){ss_late+=L[i]*L[i];} nlate+=B; }
  }
  printf("RMS (4-5 s, nach 256 Noten): %.4f | Rechenzeit %.1f s fuer 5 s Audio\n",sqrt(ss_late/nlate),tp);
  p->stop_processing(p); p->destroy(p); entry->deinit(); return 0;
}
