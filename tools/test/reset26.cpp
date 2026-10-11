// Issue #26: reset types (GM / GS / GM2) and SC-88 system mode (Mode 1 / Mode 2).
//   1) effect: part 1 set to Strings + CC7 40, then reset -> next note again like after boot?
//   2) ready time: note-on d ms after the reset (same block, frame-exact) -> does it sound normally?
//   3) SC-88 family: system mode at boot, after Mode 1 / Mode 2 (port B independent?)
//   4) plugin menu commands (ui_command 2/6/7/8/9): reset sent to all units, notes held back
// usage: reset26 <plugin-id>   (UI=0: skip part 4, e.g. for the unmodified plugin)
#include "clap/clap.h"
#include "nuked_sc55.h"
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
static double acc; static double dc; static long smp=0, onset=-1;
static void block(std::vector<Ev> e){ evs=std::move(e); ptr.clear();
  for(auto&x:evs){ if(x.sx){ x.s.buffer=x.b.data(); x.s.size=x.b.size(); ptr.push_back(&x.s.header);} else ptr.push_back(&x.m.header);}
  p->process(p,&pr); for(int i=0;i<512;i++){ double v=L[i]-dc; acc+=v*v; if(onset<0&&std::fabs(v)>2e-3) onset=smp+i; } smp+=512; }
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
int main(int argc,char**argv){
  const char* id=argc>1?argv[1]:"net.nuked_sc55_poly_clap.sc55_v1_21";
  const bool ui=!getenv("UI")||atoi(getenv("UI"));
  clap_host_t host{CLAP_VERSION_INIT,nullptr,"t","t","","1",[](const clap_host_t*,const char*)->const void*{return nullptr;},[](const clap_host_t*){},[](const clap_host_t*){},[](const clap_host_t*){}};
  clap_entry.init("/tmp/x/p.clap"); auto fa=(const clap_plugin_factory_t*)clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID);
  p=fa->create_plugin(fa,&host,id); if(!p||!p->init(p)){ printf("kein Plugin %s\n",id); return 1; } auto ns=(NukedSc55*)p->plugin_data;
  p->activate(p,44100,1,512); p->start_processing(p);
  float* ch[2]={L,R}; static clap_audio_buffer_t out{}; out.data32=ch; out.channel_count=2; static clap_input_events_t in{nullptr,sz,gt};
  pr.frames_count=512; pr.audio_outputs=&out; pr.audio_outputs_count=1; pr.in_events=&in;
  const bool is88 = std::strstr(id,"sc88") && !std::strstr(id,"8850");
  const bool is8850 = std::strstr(id,"8850");
  idle(3.0);
  { block({}); double s=0; for(int i=0;i<512;i++) s+=L[i]; dc=s/512; }
  const double ref=note();
  dirty(); const double dirt=note();
  printf("%s\n1) Wirkung: Boot-Note %.1f dB, nach Strings+CC7=40 %.1f dB\n",id,ref,dirt);
  struct R { const char* n; std::vector<uint8_t>* m; } rs[]={{"GM",&GM},{"GS",&GS},{"GM2",&GM2},{"Mode 1",&MO1},{"Mode 2",&MO2}};
  const bool only4=getenv("ONLY4");
  if(!only4) for(auto& r: rs){ dirty(); block({S(*r.m)}); idle(1.5); const double a=note(); printf("   %-6s danach %.1f dB (%s)\n",r.n,a,std::fabs(a-ref)<1.0?"zurueckgesetzt":"NICHT zurueckgesetzt"); }
  if(!only4) printf("2) Bereitschaft: Note d ms nach dem Reset (Boot-Pegel %.1f dB)\n",ref);
  if(!only4) for(auto& r: rs){ printf("   %-6s",r.n);
    for(int d: {0,20,50,100,150,200,250,300,500}){
      dirty(); const int fr=(int)(d*44.1); std::vector<Ev> v{S(*r.m,0,0)}; onset=-1; const long t0=smp;
      int blocks=fr/512; if(blocks==0) v.push_back(M(0x90,60,110,0,fr)); block(v);
      for(int i=1;i<blocks;i++) block({}); if(blocks>0) block({M(0x90,60,110,0,fr%512)});
      const double a=meas(); const long on=onset; block({M(0x80,60,0)}); idle(1.0);
      printf(" %d:%s/%.0f",d,std::fabs(a-ref)<0.3?"ok":(a<-90?"still":"falsch"),on<0?-1.0:(on-t0)/44.1-d); }
    printf("\n"); }
  if(is88&&(!only4||getenv("ONLY3"))){
    printf("3) Systemmodus: B01 bekommt Strings (Port B), Note auf Port A / Port B\n");
    auto test=[&](const char* n){ alloff(); block({M(0xC0,0,0,0),M(0xB0,7,100,0)}); block({M(0xC0,48,0,1),M(0xB0,7,40,1)}); idle(0.3);
      const double a=note(0), b=note(1); printf("   %-10s Port A %.1f dB, Port B %.1f dB -> %s\n",n,a,b,
        std::fabs(a-b)>3?"B unabhaengig (32 Parts)":"B = A (16 Parts)"); };
    // voices of one part on one unit (Mode 1 / Mode 2 may split the voices between A and B)
    auto voices=[&](const char* n){ alloff(); ns->SetMaxVoices(64); idle(0.3);
      std::vector<Ev> v{M(0xC0,16,0,0),M(0xC0,16,0,1)}; block(v); idle(0.2); v.clear();
      for(int k=0;k<70;k++) v.push_back(M(0x90,30+k,100,0)); block(v); idle(0.3); const int a=ns->ui_voices.load();
      v.clear(); for(int k=0;k<70;k++) v.push_back(M(0x90,30+k,100,1)); block(v); idle(0.3); const int b=ns->ui_voices.load();
      v.clear(); for(int port=0;port<2;port++) for(int k=0;k<70;k++) v.push_back(M(0x80,30+k,0,port)); block(v); idle(1.5);
      ns->SetMaxVoices(256); printf("   %-10s 70 Orgelnoten A01: %d Stimmen, dazu 70 auf B01: %d Stimmen (1 Einheit)\n",n,a,b); };
    voices("Boot"); block({S(MO1)}); idle(2.0); voices("Mode 1"); block({S(MO2)}); idle(2.0); voices("Mode 2");
    block({S(GS)}); idle(2.0); voices("GS danach"); block({S(MO1)}); idle(2.0); voices("Mode 1"); block({S(GS)}); idle(2.0); voices("GS danach");
    test("Boot");
    block({S(MO1)}); idle(2.0); test("Mode 1");
    block({S(MO2)}); idle(2.0); test("Mode 2");
    block({S(MO1)}); idle(2.0); test("Mode 1");
    block({S(GS)}); idle(2.0); test("GS danach");
  }
#ifndef NO_UI
  if(ui){
    printf("4) Menue-Befehle (alle Einheiten, Noten zurueckgehalten)\n");
    // wake all units, dirty part 1 everywhere, then reset via the menu and play a chord at once
    std::vector<Ev> on; for(int k=0;k<200;k++) on.push_back(M(0x91,30+k%70,60)); block(on); idle(0.5);
    std::vector<Ev> off; for(int k=0;k<200;k++) off.push_back(M(0x81,30+k%70,0)); block(off); idle(1.0);
    { dirty(); onset=-1; const long t0=smp; block({M(0x90,60,110)}); meas(); const long on=onset; block({M(0x80,60,0)}); idle(1.5);
      printf("   ohne Reset: Einsatz nach %.0f ms\n",(on-t0)/44.1); }
    struct C { const char* n; int cmd; } cs[]={{"GM",6},{"GS",2},{"GM2",7},{"Mode 1",8},{"Mode 2",9}};
    for(auto& c: cs){
      if((c.cmd==7&&!is8850)||(c.cmd>=8&&!is88)) continue;
      int awake0=ns->NumAwake(); dirty(); ns->ui_command=c.cmd; onset=-1; const long t0=smp; block({M(0x90,60,110)}); const double a=meas(); const long on=onset; block({M(0x80,60,0)}); idle(1.5);
      printf("   %-6s Note im selben Block: %.1f dB (%s), Einsatz nach %.0f ms, wach %d -> %d, Modus %d\n",c.n,a,std::fabs(a-ref)<1.0?"ok":"FALSCH",
             on<0?-1.0:(on-t0)/44.1,awake0,ns->NumAwake(),ns->system_mode.load());
    }
    // units woken after a menu reset replay it (state log): many notes on fresh units
    dirty(); ns->ui_command=2; idle(1.0);
    std::vector<Ev> ch2; for(int k=0;k<200;k++) ch2.push_back(M(0x90,30+k%70,60)); block(ch2); idle(0.3);
    std::vector<Ev> of2; for(int k=0;k<200;k++) of2.push_back(M(0x80,30+k%70,0)); block(of2); idle(1.0);
    printf("   nach GS + 200 Noten: wach %d, Note %.1f dB\n",ns->NumAwake(),note());
  }
#endif
  return 0; }
