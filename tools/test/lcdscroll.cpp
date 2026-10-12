// Issue #36: traces unit 0's LCD text (DDRAM) over song time, one line per change.
// lcdscroll <plugin-id> <events.txt> <sek> [t0]   (lines before t0 are not printed)
// HOLD="<t0> <t1> <bit>": Panel-Taste (Bit) von t0 bis t1 halten
// OUT=<datei>: Ausgang als float32 (L/R verschachtelt) für den Klangvergleich
#include "clap/clap.h"
#include "nuked_sc55.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
  const double sr=44100; const uint32_t B=getenv("BLK")?atoi(getenv("BLK")):512; p->activate(p,sr,1,B); p->start_processing(p);
  std::vector<float> L(B),R(B); float* ch[2]={L.data(),R.data()}; clap_audio_buffer_t out{}; out.data32=ch; out.channel_count=2;
  clap_input_events_t in{nullptr,sz,gt}; clap_process_t pr{}; pr.frames_count=B; pr.audio_outputs=&out; pr.audio_outputs_count=1; pr.in_events=&in;
  double dur=atof(a[3]), t0=a[4]?atof(a[4]):0; size_t ei=0; char last[90]=""; FILE* raw=getenv("OUT")?fopen(getenv("OUT"),"wb"):nullptr;
  for(long pos=0;pos<dur*sr;pos+=B){
    blk.clear(); ptr.clear();
    while(ei<ev.size() && ev[ei].t*sr < pos+B){ Slot s{}; uint32_t off=(uint32_t)std::max(0.0,ev[ei].t*sr-pos); auto&d=ev[ei].d;
      if(d[0]==0xF0){ s.sx=true; s.s.header={sizeof(clap_event_midi_sysex_t),off,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI_SYSEX,0}; s.s.buffer=d.data(); s.s.size=d.size(); }
      else { s.sx=false; s.m.header={sizeof(clap_event_midi_t),off,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI,0}; for(size_t i=0;i<d.size()&&i<3;i++) s.m.data[i]=d[i]; }
      blk.push_back(s); ei++; }
    for(auto&s:blk) ptr.push_back(s.sx?&s.s.header:&s.m.header);
    if(getenv("HOLD")){ double h0,h1; int hb; sscanf(getenv("HOLD"),"%lf %lf %d",&h0,&h1,&hb); ns->ui_panel_buttons=(pos/sr>=h0&&pos/sr<h1)?(1u<<hb):0u; }
    p->process(p,&pr);
    if(raw) for(uint32_t i=0;i<B;i++){ fwrite(&L[i],4,1,raw); fwrite(&R[i],4,1,raw); }
    NukedSc55::LcdSnapshot s; if(!ns->GetLcd(0,s)) continue;
    char txt[90]; int k=0; for(int i=0;i<80;i++){ if(i==40) txt[k++]='|'; uint8_t c=s.dd[i]; txt[k++]=(c>=32&&c<127)?c:'.'; } txt[k]=0;
    if(strcmp(txt,last)){ strcpy(last,txt); if(pos/sr>=t0) printf("%8.3f aw=%d %s\n",pos/sr,ns->NumAwake(),txt); }
  }
  if(raw) fclose(raw);
}
