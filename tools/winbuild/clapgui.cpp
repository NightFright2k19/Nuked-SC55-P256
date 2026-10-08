#include <windows.h>
#include "clap/clap.h"
#include <cstdio>
int main(int,char**a){
  HMODULE h=LoadLibraryA(a[1]); auto entry=(const clap_plugin_entry_t*)GetProcAddress(h,"clap_entry"); entry->init(a[1]);
  auto f=(const clap_plugin_factory_t*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
  clap_host_t host{CLAP_VERSION_INIT,nullptr,"t","t","","1",[](const clap_host_t*,const char*)->const void*{return nullptr;},[](const clap_host_t*){},[](const clap_host_t*){},[](const clap_host_t*){}};
  auto p=f->create_plugin(f,&host,f->get_plugin_descriptor(f,0)->id); p->init(p); p->activate(p,44100,1,512);
  auto gui=(const clap_plugin_gui_t*)p->get_extension(p,CLAP_EXT_GUI);
  auto st=(const clap_plugin_state_t*)p->get_extension(p,CLAP_EXT_STATE);
  printf("GUI-Ext: %s | Win32: %d\n",gui?"ja":"nein",gui?gui->is_api_supported(p,CLAP_WINDOW_API_WIN32,false):0);
  uint32_t w=0,hh=0; gui->create(p,CLAP_WINDOW_API_WIN32,false); gui->get_size(p,&w,&hh); printf("Größe %ux%u\n",w,hh);
  WNDCLASSA wc{}; wc.lpfnWndProc=DefWindowProcA; wc.hInstance=GetModuleHandle(0); wc.lpszClassName="H"; RegisterClassA(&wc);
  HWND hw=CreateWindowA("H","T",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,w+16,hh+40,0,0,wc.hInstance,0);
  clap_window_t win{CLAP_WINDOW_API_WIN32,{.win32=hw}}; printf("set_parent: %d show: %d\n",gui->set_parent(p,&win),gui->show(p));
  // State speichern
  static char buf[128]; static int len=0;
  clap_ostream_t os{nullptr,[](const clap_ostream_t*,const void* d,uint64_t n)->int64_t{memcpy(buf+len,d,n);len+=n;return n;}};
  bool ok=st->save(p,&os); printf("state save: %d -> '%.*s'\n",ok,len,buf);
  static const char* in_s="NSC55P1 max_voices=64"; static int pos=0;
  clap_istream_t is{nullptr,[](const clap_istream_t*,void* d,uint64_t n)->int64_t{int r=(int)strlen(in_s)-pos; if(r<=0) return 0; if((uint64_t)r>n) r=(int)n; memcpy(d,in_s+pos,r); pos+=r; return r;}};
  bool ok2=st->load(p,&is); len=0; st->save(p,&os); printf("state load '%s': %d -> jetzt '%.*s'\n",in_s,ok2,len,buf);
  for(int i=0;i<30;i++){ MSG m; while(PeekMessage(&m,0,0,0,PM_REMOVE)) DispatchMessage(&m); Sleep(30);} 
  gui->destroy(p); DestroyWindow(hw); p->deactivate(p); p->destroy(p); puts("CLAP-GUI ok"); return 0; }
