#include "clap/clap.h"
#include "nuked_sc55.h"
#include <chrono>
#include <cstdio>
extern "C" const clap_plugin_entry_t clap_entry;
int main(int,char**a){
  clap_host_t host{CLAP_VERSION_INIT,nullptr,"t","t","","1",[](const clap_host_t*,const char*)->const void*{return nullptr;},[](const clap_host_t*){},[](const clap_host_t*){},[](const clap_host_t*){}};
  clap_entry.init("/tmp/x/p.clap"); auto fa=(const clap_plugin_factory_t*)clap_entry.get_factory(CLAP_PLUGIN_FACTORY_ID);
  auto t0=std::chrono::steady_clock::now(); auto p=fa->create_plugin(fa,&host,a[1]); p->init(p); auto t1=std::chrono::steady_clock::now();
  p->activate(p,44100,1,512); auto t2=std::chrono::steady_clock::now();
  auto s=[](auto x,auto y){return std::chrono::duration<double>(y-x).count();};
  printf("  Init %.2f s + Aktivieren %.2f s = %.2f s (%d Einheiten)\n",s(t0,t1),s(t1,t2),s(t0,t2),((NukedSc55*)p->plugin_data)->NumInstances()); }
