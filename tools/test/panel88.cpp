#include "clap/clap.h"
#include "nuked_sc55.h"
#include <cmath>
#include <cstdio>
#include <vector>
extern "C" const clap_plugin_entry_t clap_entry;
static std::vector<clap_event_midi_t> evs; static std::vector<const clap_event_header_t*> ptr;
static uint32_t sz(const clap_input_events_t*){return ptr.size();}
static const clap_event_header_t* gt(const clap_input_events_t*,uint32_t i){return ptr[i];}
static const clap_plugin_t* p; static clap_process_t pr; static float L[512],R[512];
static double block(std::vector<clap_event_midi_t> e){ evs=e; ptr.clear(); for(auto&m:evs) ptr.push_back(&m.header); p->process(p,&pr); double s=0; for(int i=0;i<512;i++) s+=L[i]*(double)L[i]; return s; }
static clap_event_midi_t M(int a,int b,int c){ clap_event_midi_t m{}; m.header={sizeof(m),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI,0}; m.data[0]=a;m.data[1]=b;m.data[2]=c; return m; }
static double idle(double s){ double a=0; int n=(int)(s*44100/512); for(int i=0;i<n;i++) a+=block({}); return std::sqrt(a/(n*512.0)); }
static double note(int ch,int key){ block({M(0x90|ch,key,110)}); double r=idle(0.5); block({M(0x80|ch,key,0)}); idle(2.0); return r; }
struct OS { std::vector<char> d; static int64_t w(const clap_ostream_t* s,const void* b,uint64_t n){ auto o=(OS*)s->ctx; o->d.insert(o->d.end(),(const char*)b,(const char*)b+n); return n; } };
int main(){
  clap_host_t host{CLAP_VERSION_INIT,nullptr,"t","t","","1",[](const clap_host_t*,const char*)->const void*{return nullptr;},[](const clap_host_t*){},[](const clap_host_t*){},[](const clap_host_t*){}};
  clap_entry.init("/tmp/x/p.clap"); auto fa=(const clap_plugin_factory_t*)clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID);
  p=fa->create_plugin(fa,&host,"net.nuked_sc55_poly_clap.sc88pro"); p->init(p); auto ns=(NukedSc55*)p->plugin_data;
  p->activate(p,44100,1,512); p->start_processing(p);
  float* ch[2]={L,R}; static clap_audio_buffer_t out{}; out.data32=ch; out.channel_count=2; static clap_input_events_t in{nullptr,sz,gt};
  pr.frames_count=512; pr.audio_outputs=&out; pr.audio_outputs_count=1; pr.in_events=&in;
  idle(0.5); block({M(0xC0,0,0),M(0xC3,40,0)}); idle(0.3);
  printf("VOLUME: Standard %.2f\n",ns->volume.load());
  const double r1=note(0,60);
  ns->volume=0.5f; idle(0.1); const double r2=note(0,60);
  printf("  Mitte (0.5): Pegel %.5f statt %.5f -> %.1f dB (erwartet -30.0 dB)\n",r2,r1,20*std::log10(r2/r1));
  ns->volume=0.0f; idle(0.1); printf("  ganz links: Pegel %.6f (erwartet 0)\n",note(0,60));
  ns->volume=1.0f; idle(0.2);
  printf("MUTE Part 1: ");
  ns->mute_mask=1u; idle(0.1); const double m1=note(0,60); const double m4=note(3,60);
  printf("Part 1 Pegel %.6f (erwartet 0), Part 4 weiterhin %.5f\n",m1,m4);
  ns->mute_mask=0u; idle(0.1); printf("  nach Aufheben: Part 1 Pegel %.5f\n",note(0,60));
  printf("PREVIEW (Part 4, Violine, Prevw Note 72 = C5): ");
  ns->preview_note=72; ns->ui_preview_part=3; ns->ui_preview=1; block({}); const double pv=idle(0.5); const int vo=ns->ui_voices.load();
  ns->ui_preview=2; block({}); const double rel=idle(1.5); idle(1.5); const double after=idle(0.5);
  printf("gehalten Pegel %.5f, %d Stimmen; nach Loslassen Ausklang %.5f, danach %.6f\n",pv,vo,rel,after);
  ns->volume=0.75f; ns->preview_note=48;
  OS os; clap_ostream_t st{&os,OS::w}; auto sx=(const clap_plugin_state_t*)p->get_extension(p,CLAP_EXT_STATE); sx->save(p,&st);
  printf("Zustand: %.*s\n",(int)os.d.size(),os.d.data());
  return 0; }
