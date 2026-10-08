#include "clap/clap.h"
#include "nuked_sc55.h"
#include <cstdio>
#include <cstring>
extern "C" const clap_plugin_entry_t clap_entry;
static long rss(){ FILE* f=fopen("/proc/self/status","r"); char l[256]; long v=0; while(fgets(l,256,f)) if(!strncmp(l,"VmRSS:",6)) v=atol(l+6); fclose(f); return v/1024; }
static uint32_t sz(const clap_input_events_t*){return 0;}
static const clap_event_header_t* gt(const clap_input_events_t*,uint32_t){return nullptr;}
int main(int,char**a){
  long r0=rss();
  clap_host_t host{CLAP_VERSION_INIT,nullptr,"t","t","","1",[](const clap_host_t*,const char*)->const void*{return nullptr;},[](const clap_host_t*){},[](const clap_host_t*){},[](const clap_host_t*){}};
  clap_entry.init("/tmp/x/p.clap"); auto fa=(const clap_plugin_factory_t*)clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID);
  auto p=fa->create_plugin(fa,&host,a[1]); p->init(p); long r1=rss(); p->activate(p,44100,1,512); long r2=rss();
  auto ns=(NukedSc55*)p->plugin_data;
  printf("%s: %d Einheiten | RAM nach Init %ld MB, nach Aktivieren %ld MB (Programm vorher %ld MB) -> %.1f MB je Einheit\n",a[1],ns->NumInstances(),r1,r2,r0,(r2-r0)/(double)ns->NumInstances()); }
