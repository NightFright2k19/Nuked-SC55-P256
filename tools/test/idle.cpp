#include "clap/clap.h"
#include "nuked_sc55.h"
#include <chrono>
#include <cstdio>
extern "C" const clap_plugin_entry_t clap_entry;
static uint32_t sz(const clap_input_events_t*){return 0;}
static const clap_event_header_t* gt(const clap_input_events_t*,uint32_t){return nullptr;}
int main(int,char**a){
  clap_host_t host{CLAP_VERSION_INIT,nullptr,"t","t","","1",[](const clap_host_t*,const char*)->const void*{return nullptr;},[](const clap_host_t*){},[](const clap_host_t*){},[](const clap_host_t*){}};
  clap_entry.init("/tmp/x/p.clap"); auto fa=(const clap_plugin_factory_t*)clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID);
  auto p=fa->create_plugin(fa,&host,a[1]); p->init(p); auto ns=(NukedSc55*)p->plugin_data; p->activate(p,44100,1,512); p->start_processing(p);
  static float L[512],R[512]; float* ch[2]={L,R}; clap_audio_buffer_t out{}; out.data32=ch; out.channel_count=2; clap_input_events_t in{nullptr,sz,gt};
  clap_process_t pr{}; pr.frames_count=512; pr.audio_outputs=&out; pr.audio_outputs_count=1; pr.in_events=&in;
  for(int b=0;b<200;b++) p->process(p,&pr);
  const int n=(int)(20*44100/512); auto t0=std::chrono::steady_clock::now(); for(int b=0;b<n;b++) p->process(p,&pr);
  double s=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
  printf("%s: Stille 20 s, %d Einheit(en) wach -> %.1f %% eines Kerns\n",a[1],ns->NumAwake(),100*s/20); }
