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
static void idle(double s){ for(int i=0;i<(int)(s*44100/512);i++) block({}); }
static double note(int ch,int key){ block({M(0x90|ch,key,110)}); double s=0; int n=(int)(0.5*44100/512); for(int i=0;i<n;i++) s+=block({}); block({M(0x80|ch,key,0)}); idle(2.0); return std::sqrt(s/(n*512.0)); }
int main(){
  clap_host_t host{CLAP_VERSION_INIT,nullptr,"t","t","","1",[](const clap_host_t*,const char*)->const void*{return nullptr;},[](const clap_host_t*){},[](const clap_host_t*){},[](const clap_host_t*){}};
  clap_entry.init("/tmp/x/p.clap"); auto fa=(const clap_plugin_factory_t*)clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID);
  p=fa->create_plugin(fa,&host,"net.nuked_sc55_poly_clap.sc88pro"); p->init(p); auto ns=(NukedSc55*)p->plugin_data;
  p->activate(p,44100,1,512); p->start_processing(p);
  float* ch[2]={L,R}; static clap_audio_buffer_t out{}; out.data32=ch; out.channel_count=2; static clap_input_events_t in{nullptr,sz,gt};
  pr.frames_count=512; pr.audio_outputs=&out; pr.audio_outputs_count=1; pr.in_events=&in;
  idle(0.5); block({M(0xC0,0,0),M(0xC3,0,0)}); idle(0.3);
  const char* nm[3]={"SC-55","SC-88","SC-88 Pro"};
  double r[3]; printf("Start-Map laut Plugin: %s\n",nm[ns->tone_map.load()]);
  double a=note(0,60); printf("Piano, Start (Kanal 1): RMS %.5f\n",a);
  for(int m : {2,0,1}){ ns->tone_map=m; idle(1.5); r[m]=note(0,60); printf("nach Umschalten auf %-9s: RMS %.5f  (Verhaeltnis zu SC-88 Pro: %.3f)\n",nm[m],r[m],0.0); }
  for(int m=0;m<3;m++) printf("  %-9s / SC-88 Pro = %.3f   (Referenz 88emu direkt: SC-55 1.373, SC-88 1.705)\n",nm[m],r[m]/r[2]);
  printf("Start war %s\n", std::fabs(a-r[2])<std::fabs(a-r[1])&&std::fabs(a-r[2])<std::fabs(a-r[0])?"SC-88 Pro (Werkseinstellung, wie gewuenscht)":"NICHT SC-88 Pro");
  // Map aller Einheiten (auch schlafender) laut Panel-LEDs, nach Umschalten auf SC-55
  ns->tone_map=0; idle(2.5); idle(1.0);
  printf("Einheiten-Map nach Umschalten auf SC-55 (wach: %d):",ns->NumAwake()); for(int i=0;i<ns->NumInstances();i++) printf(" E%d=%s",i,nm[ns->UnitToneMap(i)]); printf("\n");
  std::vector<clap_event_midi_t> on; block({M(0xC1,16,0)}); for(int k=0;k<70;k++) on.push_back(M(0x91,30+k%60,60)); block(on);
  idle(1.5); printf("Nach 70 Orgelnoten wach: %d, Maps:",ns->NumAwake()); for(int i=0;i<ns->NumInstances();i++) printf(" E%d=%s",i,nm[ns->UnitToneMap(i)]); printf("\n");
  std::vector<clap_event_midi_t> off; for(int k=0;k<70;k++) off.push_back(M(0x81,30+k%60,0)); block(off); idle(4);
  printf("Nach Loslassen + 4 s wach: %d\n",ns->NumAwake());
  char buf[80]; void* d=nullptr; (void)d; printf("Chunk: max_voices=%d map=%d\n",ns->max_voices.load(),ns->tone_map.load());
  return 0; }
