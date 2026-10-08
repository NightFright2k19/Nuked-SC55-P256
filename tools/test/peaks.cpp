#include "clap/clap.h"
#include "nuked_sc55.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>
extern "C" const clap_plugin_entry_t clap_entry;
struct E{double t; std::vector<uint8_t> d;};
static std::vector<clap_event_midi_t> blk; static std::vector<const clap_event_header_t*> ptr;
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
  size_t ei=0; int win_max=0; int over22=0, over24=0; int secs_over22=0, secs_over24=0; long blocks=0;
  printf("Max. klingende Partials je Sekunde (alle Einheiten):\n");
  for(long pos=0;pos<atof(a[3])*sr;pos+=B){
    blk.clear(); ptr.clear();
    while(ei<ev.size() && ev[ei].t*sr < pos+B){ auto&d=ev[ei].d; if(d[0]!=0xF0){ clap_event_midi_t m{}; m.header={sizeof(m),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI,0}; for(size_t i=0;i<d.size()&&i<3;i++) m.data[i]=d[i]; blk.push_back(m);} ei++; }
    for(auto&m:blk) ptr.push_back(&m.header);
    p->process(p,&pr);
    int act=0; for(int i=0;i<ns->NumInstances();i++) if(ns->Diag(i).awake) act+=ns->ActivePartials(i);
    win_max=std::max(win_max,act); blocks++;
    if(blocks % (long)(sr/B) == 0){ printf("%3d ",win_max); if(win_max>22) secs_over22++; if(win_max>24) secs_over24++; if((blocks/(long)(sr/B))%20==0) printf("\n"); win_max=0; }
  }
  printf("\nSekunden mit > 22 Partials: %d, mit > 24 Partials: %d (von %d)\n",secs_over22,secs_over24,(int)(blocks/(long)(sr/B)));
}
