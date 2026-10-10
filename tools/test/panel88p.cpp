// SC-88 Pro (Issue #19): Original-Panel, alle Tasten an Einheit 0; Panel-Aenderungen (Firmware-Protokoll
// im Arbeitsspeicher) gehen an die anderen Einheiten. Vergleich des Arbeitsspeichers der Einheiten.
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
#include <sstream>
#include <thread>
#include "clap/clap.h"
#include "speex/speex_resampler.h"
#define private public
#include "nuked_sc55.h"
#undef private
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
extern "C" const clap_plugin_entry_t clap_entry;
struct Ev { clap_event_midi_t m; clap_event_midi_sysex_t s; std::vector<uint8_t> b; bool sx; };
static std::vector<Ev> evs; static std::vector<const clap_event_header_t*> ptr;
static uint32_t sz(const clap_input_events_t*){return ptr.size();}
static const clap_event_header_t* gt(const clap_input_events_t*,uint32_t i){return ptr[i];}
static const clap_plugin_t* p; static clap_process_t pr; static float L[512],R[512];
static double sl,sr;
static void block(std::vector<Ev> e){ evs=std::move(e); ptr.clear();
  for(auto&x:evs){ if(x.sx){ x.s.buffer=x.b.data(); x.s.size=x.b.size(); ptr.push_back(&x.s.header);} else ptr.push_back(&x.m.header);}
  p->process(p,&pr); for(int i=0;i<512;i++){ sl+=L[i]*(double)L[i]; sr+=R[i]*(double)R[i]; } }
static Ev M(int a,int b,int c,int port=0){ Ev e{}; e.m.header={sizeof(e.m),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI,0}; e.m.port_index=port; e.m.data[0]=a;e.m.data[1]=b;e.m.data[2]=c; return e; }
static void idle(double s){ for(int i=0;i<(int)(s*44100/512);i++) block({}); }
static double rms(double s,double secs){ return std::sqrt(s/(44100*secs)); }
static void note(std::vector<Ev> pre,int ch,int key,int port,double& l,double& r){
  block(pre); block({M(0x90|ch,key,110,port)}); sl=sr=0; for(int i=0;i<(int)(0.5*44100/512);i++) block({});
  l=rms(sl,0.5); r=rms(sr,0.5); block({M(0x80|ch,key,0,port)}); idle(1.5); }
// Arbeitsspeicher: verglichen wird der Bereich der GS-Parameter (System 5000.., Parts A ab 5100 je 70h,
// Parts B ab 6600; gefunden per DT1 mit Kennwerten). Adressen, an denen sich die Einheiten schon im
// Ruhezustand unterscheiden, werden vorher gesammelt und ausgelassen.
constexpr int kLo = 0x5000, kHi = 0x7400;
static std::vector<bool> noise(65536,false);
static void learn(NukedSc55* ns){ static uint8_t x[65536],y[65536];
  emu88_peek_work_ram(ns->instances[0].ctx,0,x,65536);
  for(int u=1;u<ns->NumInstances();u++){ emu88_peek_work_ram(ns->instances[u].ctx,0,y,65536); for(int i=0;i<65536;i++) if(x[i]!=y[i]) noise[i]=true; } }
static int ramdiff(NukedSc55* ns,const char* what){ static uint8_t x[65536],y[65536]; int tot=0;
  emu88_peek_work_ram(ns->instances[0].ctx,0,x,65536); printf("   RAM %s:",what);
  for(int u=1;u<ns->NumInstances();u++){ emu88_peek_work_ram(ns->instances[u].ctx,0,y,65536); int n=0;
    for(int i=kLo;i<kHi;i++) if(!noise[i]&&x[i]!=y[i]){ if(n<4) printf(" E%d %04X:%02X/%02X",u,i,x[i],y[i]); n++; }
    printf(" [E%d: %d]",u,n); tot+=n;
    if(u==1&&getenv("PAGES")){ int pg[256]={}; for(int i=kLo;i<kHi;i++) if(!noise[i]&&x[i]!=y[i]) pg[i>>8]++; printf("\n     Seiten:"); for(int q=0;q<256;q++) if(pg[q]) printf(" %02X:%d",q,pg[q]); printf("\n    "); } }
  printf("\n"); return tot; }
static void press(NukedSc55* ns,int bit,double hold=0.12){ ns->ui_panel_buttons=1u<<bit; idle(hold); ns->ui_panel_buttons=0; idle(0.35); }
static std::string lcd(NukedSc55* ns){ NukedSc55::LcdSnapshot s; std::string r; if(!ns->GetLcd(0,s)) return "?";
  for(int i=0;i<20;i++){ char c=s.dd[i]; r+= (c>=32&&c<127)?c:'.'; } r+=" | "; for(int i=40;i<58;i++){ char c=s.dd[i]; r+=(c>=32&&c<127)?c:'.'; } return r; }
struct Step { const char* name; int bit; int n; };
int main(){
  clap_host_t host{CLAP_VERSION_INIT,nullptr,"t","t","","1",[](const clap_host_t*,const char*)->const void*{return nullptr;},[](const clap_host_t*){},[](const clap_host_t*){},[](const clap_host_t*){}};
  clap_entry.init("/tmp/x/p.clap"); auto fa=(const clap_plugin_factory_t*)clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID);
  p=fa->create_plugin(fa,&host,"net.nuked_sc55_poly_clap.sc88pro"); if(!p){ printf("kein Plugin\n"); return 1; }
  p->init(p); auto ns=(NukedSc55*)p->plugin_data;
  if(!p->activate(p,44100,1,512)){ printf("activate fehlgeschlagen\n"); return 1; }
  p->start_processing(p);
  float* ch[2]={L,R}; static clap_audio_buffer_t out{}; out.data32=ch; out.channel_count=2; static clap_input_events_t in{nullptr,sz,gt};
  pr.frames_count=512; pr.audio_outputs=&out; pr.audio_outputs_count=1; pr.in_events=&in;
  idle(1.5); for(int k=0;k<4;k++){ learn(ns); idle(0.4); }
  int nz=0; for(bool b:noise) nz+=b; printf("%s, Einheiten %d (wach %d), Ruhe-Unterschiede %d Byte\n",ns->ModelName(),ns->NumInstances(),ns->NumAwake(),nz);
  ramdiff(ns,"Start");
  const Step steps[]={{"INSTRUMENT >",4,3},{"LEVEL <",20,2},{"PAN >",13,1},{"REVERB <",18,1},{"CHORUS >",11,2},
    {"KEY SHIFT >",17,1},{"MIDI CH >",9,1},{"MIDI CH <",8,1},{"PART >",14,1},{"LEVEL >",21,1},{"INSTRUMENT <",3,1},
    {"USER INST",24,1},{"SELECT (EFX an)",25,1},{"EFX TYPE >",27,2},{"EFX PARAM >",29,1},{"EFX VALUE >",31,3},{"USER INST",24,1},
    {"SELECT (Vib.)",25,1},{"VIB RATE >",27,1},{"VIB DEPTH >",29,2},{"VIB DELAY <",30,1},{"SELECT (Fil.)",25,1},{"CUTOFF >",29,1},{"RESONANCE >",31,1},
    {"SELECT (Env.)",25,1},{"ATTACK >",27,1},{"DECAY <",28,1},{"RELEASE >",31,1},{"SELECT (aus)",25,1},{"PART <",22,1}};
  printf("1) Tasten (Anzeige von Einheit 0 danach)\n");
  for(const auto& s:steps){ for(int k=0;k<s.n;k++) press(ns,s.bit); printf("   %-16s x%d  '%s'\n",s.name,s.n,lcd(ns).c_str()); }
  idle(0.5); int d1=ramdiff(ns,"nach den Tasten");
  printf("2) ALL-Modus: LEVEL/PAN wirken auf alles, INSTRUMENT und MIDI CH gesperrt\n");
  press(ns,6); printf("   ALL            '%s' LED %03X\n",lcd(ns).c_str(),ns->PanelLeds());
  press(ns,21); press(ns,12); printf("   LEVEL >, PAN < '%s'\n",lcd(ns).c_str());
  press(ns,4); printf("   INSTRUMENT >   '%s' LED %03X (unveraendert erwartet)\n",lcd(ns).c_str(),ns->PanelLeds());
  press(ns,9); printf("   MIDI CH >      '%s' (unveraendert erwartet)\n",lcd(ns).c_str());
  press(ns,6); printf("   ALL aus        '%s'\n",lcd(ns).c_str()); idle(0.5);
  int d2=ramdiff(ns,"nach ALL");
  printf("3) Map-Wechsel (SC-55, ALL aus) und zurueck: keine Panel-Weitergabe\n");
  const uint64_t log0=ns->state_log.Seq();
  ns->tone_map=0; idle(2.5); printf("   SC-55 Map: Einheiten-Map %d %d %d %d, Zustandsprotokoll +%ld\n",ns->UnitToneMap(0),ns->UnitToneMap(1),ns->UnitToneMap(2),ns->UnitToneMap(3),(long)(ns->state_log.Seq()-log0));
  ns->tone_map=2; idle(2.5); int d3=ramdiff(ns,"nach Map-Wechsel");
  printf("4) Halten mit Wiederholung: LEVEL > 1,5 s gehalten\n");
  press(ns,21,1.5); printf("   '%s'\n",lcd(ns).c_str()); idle(0.5); int d4=ramdiff(ns,"nach Wiederholung");
  printf("5) Alle Einheiten wecken (200 Noten) und vergleichen\n");
  { std::vector<Ev> on; for(int k=0;k<200;k++) on.push_back(M(0x90|(k%16),30+(k/16)%60,80)); block(on); idle(1.0);
    printf("   wach %d\n",ns->NumAwake()); std::vector<Ev> off; for(int k=0;k<200;k++) off.push_back(M(0x80|(k%16),30+(k/16)%60,0)); block(off); idle(0.5); }
  const int d5=ramdiff(ns,"nach dem Wecken");
  printf("Gegenprobe: DT1 (Part 3 LEVEL 50, EFX-Typ) nur an Einheit 0\n");
  { uint8_t m1[]={0xf0,0x41,0x10,0x42,0x12,0x40,0x13,0x19,0x32,0,0xf7}, m2[]={0xf0,0x41,0x10,0x42,0x12,0x40,0x03,0x00,0x01,0x10,0,0xf7};
    for(auto* m:{m1,m2}){ const size_t n=(m==m1)?sizeof m1:sizeof m2; int sum=0; for(size_t i=5;i<n-2;i++) sum+=m[i]; m[n-2]=(128-(sum&127))&127; emu88_play_sysex(ns->instances[0].ctx,m,n); }
    idle(0.3); printf("   %s\n",ramdiff(ns,"Gegenprobe")>0?"Unterschied erkannt (Pruefung greift)":"NICHT erkannt"); }
  // Schlafende Einheiten holen die Aenderungen beim Wecken nach (NUKED_SC55_POLY_DYNAMIC=0: alle wach,
  // dann muessen auch die Zwischenstaende gleich sein)
  const bool all_awake = getenv("NUKED_SC55_POLY_DYNAMIC") && getenv("NUKED_SC55_POLY_DYNAMIC")[0] == '0';
  printf("Ergebnis: %s\n",(d5 | (all_awake ? d1 | d2 | d3 | d4 : 0)) == 0 ? "alle Einheiten gleich" : "UNTERSCHIEDE");
  return 0; }
