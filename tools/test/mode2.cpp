// Issue #26: reset types (GM / GS / GM2) and SC-88 system mode (Mode 1 / Mode 2).
//   1) effect: part 1 set to Strings + CC7 40, then reset -> next note again like after boot?
//   2) ready time: note-on d ms after the reset (same block, frame-exact) -> does it sound normally?
//   3) SC-88 family: system mode at boot, after Mode 1 / Mode 2 (port B independent?)
//   4) plugin menu commands (ui_command 2/6/7/8/9): reset sent to all units, notes held back
// usage: reset26 <plugin-id>   (UI=0: skip part 4, e.g. for the unmodified plugin)
#include "clap/clap.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <sstream>
#include <map>
#define private public
#include "nuked_sc55.h"
#undef private
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
extern "C" const clap_plugin_entry_t clap_entry;
struct Ev { clap_event_midi_t m; clap_event_midi_sysex_t s; std::vector<uint8_t> b; bool sx; };
static std::vector<Ev> evs; static std::vector<const clap_event_header_t*> ptr;
static uint32_t sz(const clap_input_events_t*){return ptr.size();}
static const clap_event_header_t* gt(const clap_input_events_t*,uint32_t i){return ptr[i];}
static const clap_plugin_t* p; static clap_process_t pr; static float L[512],R[512];
static double acc; static double dc;
static void block(std::vector<Ev> e){ evs=std::move(e); ptr.clear();
  for(auto&x:evs){ if(x.sx){ x.s.buffer=x.b.data(); x.s.size=x.b.size(); ptr.push_back(&x.s.header);} else ptr.push_back(&x.m.header);}
  p->process(p,&pr); for(int i=0;i<512;i++){ double v=L[i]-dc; acc+=v*v; } }
static Ev M(int a,int b,int c,int port=0,int t=0){ Ev e{}; e.m.header={sizeof(e.m),(uint32_t)t,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI,0}; e.m.port_index=port; e.m.data[0]=a;e.m.data[1]=b;e.m.data[2]=c; return e; }
static Ev S(std::vector<uint8_t> b,int port=0,int t=0){ Ev e{}; e.sx=true; e.b=b; e.s.header={sizeof(e.s),(uint32_t)t,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI_SYSEX,0}; e.s.port_index=port; return e; }
static void idle(double s){ for(int i=0;i<(int)(s*44100/512);i++) block({}); }
static const int NB=(int)(0.5*44100/512);
static double db(double r){ return r>1e-9?20*std::log10(r):-180; }
// level of the next 0.5 s (dBFS, DC removed)
static double meas(){ acc=0; for(int i=0;i<NB;i++) block({}); return db(std::sqrt(acc/(NB*512.0))); }
static void alloff(){ std::vector<Ev> v; for(int port=0;port<2;port++) for(int c=0;c<16;c++) v.push_back(M(0xB0|c,123,0,port)); block(v); idle(1.0); }
static std::vector<uint8_t> GM ={0xF0,0x7E,0x7F,0x09,0x01,0xF7};
static std::vector<uint8_t> GS ={0xF0,0x41,0x10,0x42,0x12,0x40,0x00,0x7F,0x00,0x41,0xF7};
static std::vector<uint8_t> GM2={0xF0,0x7E,0x7F,0x09,0x03,0xF7};
static std::vector<uint8_t> MO1={0xF0,0x41,0x10,0x42,0x12,0x00,0x00,0x7F,0x00,0x01,0xF7};
static std::vector<uint8_t> MO2={0xF0,0x41,0x10,0x42,0x12,0x00,0x00,0x7F,0x01,0x00,0xF7};
// "dirty" part 1: Strings, volume 40
static void dirty(int port=0){ block({M(0xC0,48,0,port),M(0xB0,7,40,port)}); idle(0.3); }
static double note(int port=0,int ch=0){ block({M(0x90|ch,60,110,port)}); double r=meas(); block({M(0x80|ch,60,0,port)}); idle(1.2); return r; }
static std::vector<bool> noise(65536,false);
static void learn(NukedSc55* ns){ static uint8_t x[65536],y[65536]; emu88_peek_work_ram(ns->instances[0].ctx,0,x,65536);
  for(int u=1;u<ns->NumInstances();u++){ emu88_peek_work_ram(ns->instances[u].ctx,0,y,65536); for(int i=0;i<65536;i++) if(x[i]!=y[i]) noise[i]=true; } }
static void ramdiff(NukedSc55* ns,const char* what){ static uint8_t x[65536],y[65536]; emu88_peek_work_ram(ns->instances[0].ctx,0,x,65536); printf("   RAM %s:",what);
  for(int u=1;u<ns->NumInstances();u++){ if(!ns->instances[u].awake){ printf(" [E%d schlaeft]",u); continue; } emu88_peek_work_ram(ns->instances[u].ctx,0,y,65536); int n=0;
    for(int i=0x2370;i<0x7400;i++) if((i<0x2560||i>=0x5000)&&!noise[i]&&x[i]!=y[i]){ if(n<(getenv("ALL")?400:3)) printf(" E%d %04X:%02X/%02X",u,i,x[i],y[i]); n++; } printf(" [E%d: %d]",u,n); } printf("\n"); }
// Issue #26: Mode 2 (Double Module) - resets per part group, menu resets both groups, woken units replay the mode
int main(int argc,char**argv){
  const char* id=argv[1];
  clap_host_t host{CLAP_VERSION_INIT,nullptr,"t","t","","1",[](const clap_host_t*,const char*)->const void*{return nullptr;},[](const clap_host_t*){},[](const clap_host_t*){},[](const clap_host_t*){}};
  clap_entry.init("/tmp/x/p.clap"); auto fa=(const clap_plugin_factory_t*)clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID);
  p=fa->create_plugin(fa,&host,id); p->init(p); auto ns=(NukedSc55*)p->plugin_data;
  p->activate(p,44100,1,512); p->start_processing(p);
  float* ch[2]={L,R}; static clap_audio_buffer_t out{}; out.data32=ch; out.channel_count=2; static clap_input_events_t in{nullptr,sz,gt};
  pr.frames_count=512; pr.audio_outputs=&out; pr.audio_outputs_count=1; pr.in_events=&in;
  idle(3.0); { block({}); double s=0; for(int i=0;i<512;i++) s+=L[i]; dc=s/512; }
  const double ref=note(0);
  auto st=[&](double a){ return std::fabs(a-ref)<1.0?"Boot-Klang":"verstellt"; };
  // wake all units once and learn the bytes that differ anyway
  { std::vector<Ev> v; for(int k=0;k<250;k++) v.push_back(M(0x92,20+k%90,50)); block(v); idle(0.3); v.clear(); for(int k=0;k<250;k++) v.push_back(M(0x82,20+k%90,0)); block(v); idle(0.5); learn(ns); idle(1.5); }
  printf("%s, Start: Modus-Haken %d, Mode 2 laut Log %d\n",id,ns->system_mode.load(),ns->state_log.DoubleModule());
  if(!getenv("MODE1")){ ns->ui_command=9; } idle(1.0);
  printf("1) Menue Mode 2: Haken %d, Log %d\n",ns->system_mode.load(),ns->state_log.DoubleModule());
  dirty(0); dirty(1); block({S(GS,1)}); idle(1.0);
  { double a=note(0), b=note(1); printf("2) Host-GS nur an IN B: A01 %s, B01 %s\n",st(a),st(b)); }
  // many notes on A01 wake the other units: they must replay Mode 2, the A01 edits and the B-only GS reset
  { std::vector<Ev> v; for(int k=0;k<250;k++) v.push_back(M(0x90,20+k%90,50)); block(v); idle(0.5); printf("3) 250 Noten auf A01: wach %d\n",ns->NumAwake());
    v.clear(); for(int k=0;k<250;k++) v.push_back(M(0x80,20+k%90,0)); block(v); idle(0.4); ramdiff(ns,"Einheiten vs. Einheit 0 (Modusblock + GS-Parameter)"); }
  idle(1.5); dirty(0); dirty(1); ns->ui_command=2; idle(1.0);
  { double a=note(0), b=note(1); printf("4) Menue GS in Mode 2: A01 %s, B01 %s\n",st(a),st(b)); }
  dirty(0); dirty(1); ns->ui_command=6; idle(1.0);
  { double a=note(0), b=note(1); printf("5) Menue GM in Mode 2: A01 %s, B01 %s\n",st(a),st(b)); }
  { std::vector<Ev> v; for(int k=0;k<250;k++) v.push_back(M(0x91,20+k%90,50,1)); block(v); idle(0.5); printf("6) 250 Noten auf B02: wach %d\n",ns->NumAwake());
    v.clear(); for(int k=0;k<250;k++) v.push_back(M(0x81,20+k%90,0,1)); block(v); idle(0.4); ramdiff(ns,"Einheiten vs. Einheit 0 (Modusblock + GS-Parameter)"); idle(1.6); }
  ns->ui_command=8; idle(1.0); dirty(0); dirty(1); block({S(GS,1)}); idle(1.0);
  { double a=note(0), b=note(1); printf("7) Menue Mode 1 (Haken %d, Log %d), Host-GS an IN B: A01 %s, B01 %s\n",ns->system_mode.load(),ns->state_log.DoubleModule(),st(a),st(b)); }
  block({S(MO2)}); idle(1.0); printf("8) Host sendet Mode 2: Haken %d, Log %d\n",ns->system_mode.load(),ns->state_log.DoubleModule());
  return 0; }
