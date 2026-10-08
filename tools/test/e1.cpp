#include "clap/clap.h"
#include "nuked_sc55.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>
extern "C" const clap_plugin_entry_t clap_entry;
struct E{double t; std::vector<uint8_t> d;};
struct Slot{clap_event_midi_t m; clap_event_midi_sysex_t s; bool sx;};
static std::vector<Slot> blk; static std::vector<const clap_event_header_t*> ptr;
static uint32_t sz(const clap_input_events_t*){return ptr.size();}
static const clap_event_header_t* gt(const clap_input_events_t*,uint32_t i){return ptr[i];}
int main(int,char**a){
  std::vector<E> ev; std::ifstream f(a[2]); std::string line;
  while(std::getline(f,line)){std::istringstream is(line); E e; int n; is>>e.t>>n; for(int i=0;i<n;i++){int b;is>>b;e.d.push_back(b);} ev.push_back(e);}
  clap_host_t host{CLAP_VERSION_INIT,nullptr,"t","t","","1",[](const clap_host_t*,const char*)->const void*{return nullptr;},[](const clap_host_t*){},[](const clap_host_t*){},[](const clap_host_t*){}};
  clap_entry.init("/tmp/x/p.clap"); auto fa=(const clap_plugin_factory_t*)clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID);
  auto p=fa->create_plugin(fa,&host,a[1]); p->init(p); auto ns=(NukedSc55*)p->plugin_data;
  const double sr=44100; const uint32_t B=512; p->activate(p,sr,1,B); p->start_processing(p);
  std::vector<float> L(B),R(B); float* ch[2]={L.data(),R.data()}; clap_audio_buffer_t out{}; out.data32=ch; out.channel_count=2;
  clap_input_events_t in{nullptr,sz,gt}; clap_process_t pr{}; pr.frames_count=B; pr.audio_outputs=&out; pr.audio_outputs_count=1; pr.in_events=&in;
  double dur=atof(a[3]); size_t ei=0; double tp=0, awake_sum=0; long nb=0; int maxp=0;
  int seg_maxp=0, seg_maxaw=0; double seg_aw=0; int seg_n=0;
  for(long pos=0;pos<dur*sr;pos+=B){
    blk.clear(); ptr.clear();
    while(ei<ev.size() && ev[ei].t*sr < pos+B){ Slot s{}; uint32_t off=(uint32_t)std::max(0.0,ev[ei].t*sr-pos); auto&d=ev[ei].d;
      if(d[0]==0xF0){ s.sx=true; s.s.header={sizeof(clap_event_midi_sysex_t),off,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI_SYSEX,0}; s.s.buffer=d.data(); s.s.size=d.size(); }
      else { s.sx=false; s.m.header={sizeof(clap_event_midi_t),off,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI,0}; for(size_t i=0;i<d.size()&&i<3;i++) s.m.data[i]=d[i]; }
      blk.push_back(s); ei++; }
    for(auto&s:blk) ptr.push_back(s.sx?&s.s.header:&s.m.header);
    auto t0=std::chrono::steady_clock::now(); p->process(p,&pr); tp+=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
    int act=0; for(int i=0;i<ns->NumInstances();i++) if(1) act+=ns->ActivePartials(i);
    int aw=ns->NumAwake(); awake_sum+=aw; nb++; maxp=std::max(maxp,act);
    seg_maxp=std::max(seg_maxp,act); seg_maxaw=std::max(seg_maxaw,aw); seg_aw+=aw; seg_n++;
    if(a[4] && seg_n*B>=10*sr){ printf("  %3.0f s: max %2d Partials, wach Ø %.2f / max %d\n",pos/sr,seg_maxp,seg_aw/seg_n,seg_maxaw); seg_maxp=seg_maxaw=0; seg_aw=0; seg_n=0; }
  }
  printf("%s: Rechenzeit %.2f s für %.0f s | max %d Partials | Ø wache Instanzen %.2f\n",getenv("LBL"),tp,dur,maxp,awake_sum/nb);
}
