#include "clap/clap.h"
#include "nuked_sc55.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <vector>
#include <chrono>
extern "C" const clap_plugin_entry_t clap_entry;
struct E{double t; std::vector<uint8_t> d;};
struct Slot{clap_event_midi_t m; clap_event_midi_sysex_t s; bool sx;};
static std::vector<Slot> blk; static std::vector<const clap_event_header_t*> ptr;
static uint32_t sz(const clap_input_events_t*){return ptr.size();}
static const clap_event_header_t* gt(const clap_input_events_t*,uint32_t i){return ptr[i];}
int main(int,char**a){
  std::vector<E> ev; std::ifstream f("/home/claude/wb/tools/midi/events.txt"); std::string line;
  while(std::getline(f,line)){std::istringstream is(line); E e; int n; is>>e.t>>n; for(int i=0;i<n;i++){int b;is>>b;e.d.push_back(b);} ev.push_back(e);}
  struct P{int r,n; double s,e;}; std::vector<P> probes; std::ifstream pi("/home/claude/wb/tools/midi/info.txt");
  while(std::getline(pi,line)){ if(line.rfind("probe",0)) continue; P p; sscanf(line.c_str(),"probe %d %d %lf %lf",&p.r,&p.n,&p.s,&p.e); probes.push_back(p);}
  clap_host_t host{CLAP_VERSION_INIT,nullptr,"t","t","","1",[](const clap_host_t*,const char*)->const void*{return nullptr;},[](const clap_host_t*){},[](const clap_host_t*){},[](const clap_host_t*){}};
  clap_entry.init("/tmp/x/p.clap"); auto fa=(const clap_plugin_factory_t*)clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID);
  auto p=fa->create_plugin(fa,&host,a[1]); p->init(p); auto ns=(NukedSc55*)p->plugin_data;
  const double sr=48000; const uint32_t B=256; p->activate(p,sr,1,B); p->start_processing(p);
  std::vector<float> L(B),R(B); float* ch[2]={L.data(),R.data()}; clap_audio_buffer_t out{}; out.data32=ch; out.channel_count=2;
  clap_input_events_t in{nullptr,sz,gt}; clap_process_t pr{}; pr.frames_count=B; pr.audio_outputs=&out; pr.audio_outputs_count=1; pr.in_events=&in;
  size_t ei=0; long total=(long)(82*sr); std::vector<double> sum(probes.size()),sum2(probes.size()); std::vector<long> cnt(probes.size()); std::vector<int> peak(probes.size()), awk(probes.size()); double tp=0; int awake_min=99,awake_max=0;
  std::vector<float> all; all.reserve(total);
  for(long pos=0;pos<total;pos+=B){
    blk.clear(); ptr.clear();
    while(ei<ev.size() && ev[ei].t*sr < pos+B){ Slot s{}; uint32_t off=(uint32_t)std::max(0.0,ev[ei].t*sr-pos); auto&d=ev[ei].d;
      if(d[0]==0xF0){ s.sx=true; s.s.header={sizeof(clap_event_midi_sysex_t),off,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI_SYSEX,0}; s.s.buffer=d.data(); s.s.size=d.size(); }
      else { s.sx=false; s.m.header={sizeof(clap_event_midi_t),off,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI,0}; for(size_t i=0;i<d.size()&&i<3;i++) s.m.data[i]=d[i]; }
      blk.push_back(s); ei++; }
    for(auto&s:blk) ptr.push_back(s.sx?&s.s.header:&s.m.header);
    {auto t0=std::chrono::steady_clock::now(); p->process(p,&pr); tp+=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();}
    {int aw=ns->NumAwake(); awake_min=std::min(awake_min,aw); awake_max=std::max(awake_max,aw);}
    double t=pos/sr; int act=0; for(int i=0;i<ns->NumInstances();i++) act+=ns->ActivePartials(i);
    for(size_t k=0;k<probes.size();k++){
      if(t>probes[k].s-1.6 && t<probes[k].s){ peak[k]=std::max(peak[k],act); awk[k]=std::max(awk[k],ns->NumAwake()); }
      if(t>probes[k].s+0.5 && t<probes[k].e-0.1){ for(uint32_t i=0;i<B;i++){sum[k]+=L[i];sum2[k]+=L[i]*(double)L[i];} cnt[k]+=B; } }
    for(uint32_t i=0;i<B;i++){all.push_back(L[i]);all.push_back(R[i]);}
  }
  printf("%-40s Instanzen %d\n",a[1],ns->NumInstances());
  for(size_t k=0;k<probes.size();k++){ double m=sum[k]/cnt[k], sd=sqrt(sum2[k]/cnt[k]-m*m);
    printf("  Runde %d (%3d Noten): max. %3d Stimmen, %2d Instanzen wach, Prüfton %.4f -> %s\n",probes[k].r,probes[k].n,peak[k],awk[k],sd, sd>0.002?"HÖRBAR":"STILLE"); }
  printf("  Rechenzeit gesamt: %.1f s für 82 s Audio | wache Instanzen min %d / max %d\n",tp,awake_min,awake_max);
  if(a[2]){FILE* w=fopen(a[2],"wb"); fwrite(all.data(),4,all.size(),w); fclose(w);}
  p->stop_processing(p); p->destroy(p);
}
