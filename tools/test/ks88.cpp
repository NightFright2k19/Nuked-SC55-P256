#include "clap/clap.h"
#include "nuked_sc55.h"
#include <cstdio>
#include <vector>
extern "C" const clap_plugin_entry_t clap_entry;
static std::vector<clap_event_midi_sysex_t> sx; static std::vector<std::vector<uint8_t>> data; static std::vector<const clap_event_header_t*> ptr;
static uint32_t sz(const clap_input_events_t*){return ptr.size();}
static const clap_event_header_t* gt(const clap_input_events_t*,uint32_t i){return ptr[i];}
static std::vector<uint8_t> dt1(int part_block,int addr,int val){ std::vector<uint8_t> d={0xF0,0x41,0x10,0x42,0x12,0x40,(uint8_t)part_block,(uint8_t)addr,(uint8_t)val,0,0xF7}; int s=0x40+part_block+addr+val; d[9]=(128-s%128)%128; return d; }
int main(int,char**a){
  clap_host_t host{CLAP_VERSION_INIT,nullptr,"t","t","","1",[](const clap_host_t*,const char*)->const void*{return nullptr;},[](const clap_host_t*){},[](const clap_host_t*){},[](const clap_host_t*){}};
  clap_entry.init("/tmp/x/p.clap"); auto fa=(const clap_plugin_factory_t*)clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID);
  auto p=fa->create_plugin(fa,&host,a[1]); p->init(p); auto ns=(NukedSc55*)p->plugin_data; p->activate(p,44100,1,512); p->start_processing(p);
  static float L[512],R[512]; float* ch[2]={L,R}; clap_audio_buffer_t out{}; out.data32=ch; out.channel_count=2; clap_input_events_t in{nullptr,sz,gt};
  clap_process_t pr{}; pr.frames_count=512; pr.audio_outputs=&out; pr.audio_outputs_count=1; pr.in_events=&in;
  for(int b=0;b<40;b++) p->process(p,&pr);
  data={dt1(0x11,0x16,0x46), dt1(0x12,0x16,0x34)}; // Part 1: +6, Part 2: -12
  for(auto& d:data){ clap_event_midi_sysex_t e{}; e.header={sizeof(e),0,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI_SYSEX,0}; e.buffer=d.data(); e.size=d.size(); sx.push_back(e);} for(auto&e:sx) ptr.push_back(&e.header);
  p->process(p,&pr); ptr.clear(); for(int b=0;b<80;b++) p->process(p,&pr);
  printf("Anzeigedaten Plugin: Part1 Key Shift %+d, Part2 %+d\n",ns->ui_parts[0].key_shift.load()-0x40,ns->ui_parts[1].key_shift.load()-0x40);
  NukedSc55::LcdSnapshot s; if(ns->GetLcd(0,s)){ printf("LCD der Firmware (Part 1), K SHIFT-Feld: '"); for(int i=52;i<55;i++) putchar(s.dd[i]>=32&&s.dd[i]<127?s.dd[i]:(s.dd[i]<8?'#':'?')); printf("'  (Zeichen < 8 = Sonderzeichen +/-)\n"); }
  return 0; }
