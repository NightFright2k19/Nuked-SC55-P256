#include "clap/clap.h"
#include "nuked_sc55.h"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
extern "C" const clap_plugin_entry_t clap_entry;

struct Ev { clap_event_midi_t m; };
static std::vector<Ev> evs;
static uint32_t ev_size(const clap_input_events_t*){return evs.size();}
static const clap_event_header_t* ev_get(const clap_input_events_t*,uint32_t i){return &evs[i].m.header;}
static void midi(uint32_t t,uint8_t a,uint8_t b,uint8_t c){Ev e{};e.m.header={sizeof(clap_event_midi_t),t,CLAP_CORE_EVENT_SPACE_ID,CLAP_EVENT_MIDI,0};e.m.port_index=0;e.m.data[0]=a;e.m.data[1]=b;e.m.data[2]=c;evs.push_back(e);}

int main(int argc,char**argv){
  const char* id=argv[1]; int seconds_total=atoi(argv[2]); int mode=atoi(argv[3]);
  clap_host_t host{CLAP_VERSION_INIT,nullptr,"t","t","","1",[](const clap_host_t*,const char*)->const void*{return nullptr;},
    [](const clap_host_t*){},[](const clap_host_t*){},[](const clap_host_t*){}};
  clap_entry.init("/tmp/x/plugin.clap");
  auto f=(const clap_plugin_factory_t*)clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID);
  auto p=f->create_plugin(f,&host,id); if(!p||!p->init(p)){puts("init fail");return 1;}
  auto* ns=(NukedSc55*)p->plugin_data;
  const double sr=48000; const uint32_t B=256;
  if(getenv("LIMIT")) ns->SetMaxVoices(atoi(getenv("LIMIT")));
  p->activate(p,sr,1,B); p->start_processing(p);
  std::vector<float> L(B),R(B); float* ch[2]={L.data(),R.data()};
  clap_audio_buffer_t out{}; out.data32=ch; out.channel_count=2;
  clap_input_events_t in{nullptr,ev_size,ev_get};
  clap_process_t pr{}; pr.frames_count=B; pr.audio_outputs=&out; pr.audio_outputs_count=1; pr.in_events=&in;
  long nblocks=(long)(seconds_total*sr/B); double sumsq=0; int maxact=0; double t_proc=0;
  FILE* raw=fopen(argv[4],"wb");
  for(long b=0;b<nblocks;b++){
    evs.clear(); double t=b*B/sr;
    if(b==0){ midi(0,0xC0,0,0); midi(1,0xC1,48,0); midi(2,0xB0,64,127); midi(3,0xB1,64,127);} // Piano, Strings, Sustain
    // mode 1: 1 Note; mode 2: Flut: 160 Noten in 2 s über 2 Kanäle
    if(mode==3 && b==10){ midi(0,0xB2,0,1); midi(1,0xC2,16,0);} if(mode==3 && b==20) midi(0,0x92,60,110);
    if(mode==4){ for(int k=0;k<256;k++){ long nb=20+(long)(k*512/(double)B); if(nb==b) midi(0,0x90|(k&1),(uint8_t)(24+(k*7)%80),90);} }
    if(mode==1 && b==10) midi(5,0x90,60,100);
    if(mode==2){ for(int k=0;k<160;k++){ long nb=10+(long)(k*0.0125*sr/B); if(nb==b) midi(k%B,0x90|(k&1),(uint8_t)(24+(k*7)%80),90);} }
    auto t0=std::chrono::steady_clock::now(); p->process(p,&pr);
    t_proc+=std::chrono::duration<double>(std::chrono::steady_clock::now()-t0).count();
    int act=0; for(int i=0;i<ns->NumInstances();i++) act+=ns->ActivePartials(i);
    if(act>maxact)maxact=act;
    if((mode==2||mode==4) && (b%94==0)){ printf("t=%4.2fs Partials %3d | wach %2d |",t,act,ns->NumAwake()); for(int i=0;i<ns->NumInstances();i++) printf(" %2d",ns->ActivePartials(i)); printf("\n");}
    for(uint32_t i=0;i<B;i++) sumsq+=L[i]*L[i];
    fwrite(L.data(),4,B,raw);
  }
  fclose(raw);
  printf("Instanzen: %d | max. gleichzeitig klingende Partials: %d | RMS L: %.4f | Rechenzeit %.2fs für %ds Audio\n",
    ns->NumInstances(),maxact,sqrt(sumsq/(nblocks*B)),t_proc,seconds_total);
  p->stop_processing(p); p->deactivate(p); p->destroy(p);
}
