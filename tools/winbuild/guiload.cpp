// Misst die CPU-Zeit des Oberflaechen-Threads: Editor offen, Song wird in Echtzeit abgespielt
// (Audio in eigenem Thread), Hauptthread verarbeitet nur Fensternachrichten (30-Bilder-Takt).
// usage: guiload <dll> <events.txt> <sekunden>
#include <windows.h>
#include "vst2/vst2_abi.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace vst2;
static intptr_t cb(AEffect*,int32_t op,int32_t,intptr_t,void*,float){ return op==audioMasterVersion?2400:0; }
struct E{ double t; std::vector<uint8_t> d; };
static std::vector<E> ev; static AEffect* fx; static std::atomic<bool> run{true}; static double secs;
static DWORD WINAPI Audio(void*){
  std::vector<float> L(512),R(512); float* o[2]={L.data(),R.data()}; size_t ei=0; LARGE_INTEGER f,t0,t; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&t0);
  for(long pos=0; run; pos+=512){ double a=pos/44100.0,b=(pos+512)/44100.0; if(a>secs){run=false;break;}
    std::vector<MidiEvent> me; std::vector<SysExEvent> sx; std::vector<std::vector<uint8_t>> sd;
    while(ei<ev.size()&&ev[ei].t<b){ auto& d=ev[ei].d; int off=(int)((ev[ei].t-a)*44100); if(off<0)off=0;
      if(d[0]==0xF0){ sd.push_back(d); SysExEvent s{}; s.type=kSysExType; s.byteSize=sizeof s; s.deltaFrames=off; s.dumpBytes=(int)d.size(); sx.push_back(s);} else { MidiEvent m{}; m.type=kMidiType; m.byteSize=sizeof m; m.deltaFrames=off; for(size_t i=0;i<d.size()&&i<3;i++) m.midiData[i]=d[i]; me.push_back(m);} ei++; }
    for(size_t i=0;i<sx.size();i++) sx[i].sysexDump=sd[i].data();
    std::vector<Event*> p; for(auto&m:me) p.push_back((Event*)&m); for(auto&s:sx) p.push_back((Event*)&s);
    std::vector<char> buf(sizeof(Events)+p.size()*sizeof(void*)); auto e=(Events*)buf.data(); e->numEvents=(int)p.size(); for(size_t i=0;i<p.size();i++) e->events[i]=p[i];
    fx->dispatcher(fx,effProcessEvents,0,0,e,0); fx->processReplacing(fx,nullptr,o,512);
    for(;;){ QueryPerformanceCounter(&t); if((t.QuadPart-t0.QuadPart)/(double)f.QuadPart >= b) break; Sleep(1); } }
  return 0; }
static double cpu(HANDLE h){ FILETIME c,e,k,u; GetThreadTimes(h,&c,&e,&k,&u); return ((((uint64_t)k.dwHighDateTime<<32)|k.dwLowDateTime)+(((uint64_t)u.dwHighDateTime<<32)|u.dwLowDateTime))/1e7; }
int main(int,char**a){
  { FILE* f=fopen(a[2],"r"); char line[8192]; while(fgets(line,sizeof line,f)){ char* p=line; E e; e.t=strtod(p,&p); int n=strtol(p,&p,10); for(int i=0;i<n;i++) e.d.push_back((uint8_t)strtol(p,&p,10)); ev.push_back(e);} fclose(f); }
  secs=atof(a[3]);
  HMODULE h=LoadLibraryA(a[1]); fx=((AEffect*(*)(HostCallback))GetProcAddress(h,"VSTPluginMain"))(cb);
  fx->dispatcher(fx,effOpen,0,0,0,0); fx->dispatcher(fx,effSetSampleRate,0,0,0,44100.f); fx->dispatcher(fx,effSetBlockSize,0,512,0,0); fx->dispatcher(fx,effMainsChanged,0,1,0,0);
  ERect* r=nullptr; fx->dispatcher(fx,effEditGetRect,0,0,&r,0);
  WNDCLASSA wc{}; wc.lpfnWndProc=DefWindowProcA; wc.hInstance=GetModuleHandle(0); wc.lpszClassName="GL"; RegisterClassA(&wc);
  RECT rc{0,0,r->right,r->bottom}; AdjustWindowRect(&rc,WS_OVERLAPPEDWINDOW,FALSE);
  HWND host=CreateWindowA("GL","T",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,rc.right-rc.left,rc.bottom-rc.top,0,0,wc.hInstance,0);
  fx->dispatcher(fx,effEditOpen,0,0,host,0);
  HANDLE me=GetCurrentThread(); HANDLE self; DuplicateHandle(GetCurrentProcess(),me,GetCurrentProcess(),&self,0,FALSE,DUPLICATE_SAME_ACCESS);
  double c0=cpu(self); HANDLE th=CreateThread(nullptr,0,Audio,nullptr,0,nullptr);
  while(run){ MSG m; while(PeekMessage(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessage(&m);} MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT); }
  WaitForSingleObject(th,INFINITE); double gui=cpu(self)-c0, au=cpu(th);
  printf("Oberflaeche: %.2f s CPU in %.0f s -> %.1f %% eines Kerns | Audio-Thread %.1f %%\n",gui,secs,100*gui/secs,100*au/secs);
  fx->dispatcher(fx,effEditClose,0,0,0,0); return 0; }
