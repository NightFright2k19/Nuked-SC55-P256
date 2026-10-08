// LCD-Test: spielt eine Eventliste durch ein VST2-Plugin mit offenem Editor, Screenshots zu Songzeiten.
// usage: lcdhost <dll> <events.txt> <out-prefix> <t1> [t2 ...]
#include <windows.h>
#include "vst2/vst2_abi.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace vst2;
static intptr_t cb(AEffect*,int32_t op,int32_t,intptr_t,void*,float){ return op==audioMasterVersion?2400:0; }
static void Shot(HWND w,int W,int H,const char* fn){
  HDC wdc=GetDC(w); HDC mem=CreateCompatibleDC(wdc);
  BITMAPINFO bi{}; bi.bmiHeader.biSize=sizeof(bi.bmiHeader); bi.bmiHeader.biWidth=W; bi.bmiHeader.biHeight=-H; bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32;
  void* bits=nullptr; HBITMAP bm=CreateDIBSection(mem,&bi,DIB_RGB_COLORS,&bits,nullptr,0); SelectObject(mem,bm);
  BitBlt(mem,0,0,W,H,wdc,0,0,SRCCOPY); GdiFlush();
  FILE* f=fopen(fn,"wb"); BITMAPFILEHEADER fh{0x4D42,(DWORD)(54+W*H*4),0,0,54}; BITMAPINFOHEADER ih=bi.bmiHeader;
  fwrite(&fh,sizeof fh,1,f); fwrite(&ih,sizeof ih,1,f); fwrite(bits,4,W*H,f); fclose(f); DeleteObject(bm); DeleteDC(mem); ReleaseDC(w,wdc); }
static void pump(){ MSG m; while(PeekMessage(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessage(&m);} }
struct E{ double t; std::vector<uint8_t> d; };
int main(int argc,char**argv){
  std::vector<E> ev; { FILE* f=fopen(argv[2],"r"); char line[8192]; while(fgets(line,sizeof line,f)){ char* p=line; E e; e.t=strtod(p,&p); int n=strtol(p,&p,10); for(int i=0;i<n;i++) e.d.push_back((uint8_t)strtol(p,&p,10)); ev.push_back(e);} fclose(f); }
  std::vector<double> shots; for(int i=4;i<argc;i++) shots.push_back(atof(argv[i]));
  HMODULE h=LoadLibraryA(argv[1]); auto mainf=(AEffect*(*)(HostCallback))GetProcAddress(h,"VSTPluginMain"); AEffect* fx=mainf(cb);
  fx->dispatcher(fx,effOpen,0,0,0,0); fx->dispatcher(fx,effSetSampleRate,0,0,0,44100.f); fx->dispatcher(fx,effSetBlockSize,0,512,0,0); fx->dispatcher(fx,effMainsChanged,0,1,0,0);
  ERect* r=nullptr; fx->dispatcher(fx,effEditGetRect,0,0,&r,0); int W=r->right,H=r->bottom;
  WNDCLASSA wc{}; wc.lpfnWndProc=DefWindowProcA; wc.hInstance=GetModuleHandle(0); wc.lpszClassName="LcdHost"; RegisterClassA(&wc);
  RECT rc{0,0,W,H}; AdjustWindowRect(&rc,WS_OVERLAPPEDWINDOW,FALSE);
  HWND host=CreateWindowA("LcdHost","T",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,rc.right-rc.left,rc.bottom-rc.top,0,0,wc.hInstance,0);
  fx->dispatcher(fx,effEditOpen,0,0,host,0); HWND ed=GetWindow(host,GW_CHILD);
  std::vector<float> L(512),R(512); float* o[2]={L.data(),R.data()}; size_t ei=0; size_t si=0;
  for(long pos=0; si<shots.size(); pos+=512){
    double t0=pos/44100.0, t1=(pos+512)/44100.0;
    std::vector<MidiEvent> me; std::vector<SysExEvent> sx; std::vector<std::vector<uint8_t>> sxd;
    while(ei<ev.size() && ev[ei].t < t1){ auto& d=ev[ei].d; int off=(int)((ev[ei].t-t0)*44100); if(off<0) off=0;
      if(d[0]==0xF0){ sxd.push_back(d); SysExEvent s{}; s.type=kSysExType; s.byteSize=sizeof s; s.deltaFrames=off; s.dumpBytes=(int)d.size(); sx.push_back(s);} 
      else { MidiEvent m{}; m.type=kMidiType; m.byteSize=sizeof m; m.deltaFrames=off; for(size_t i=0;i<d.size()&&i<3;i++) m.midiData[i]=d[i]; me.push_back(m);} ei++; }
    for(size_t i=0;i<sx.size();i++) sx[i].sysexDump=sxd[i].data();
    std::vector<Event*> ptrs; for(auto&m:me) ptrs.push_back((Event*)&m); for(auto&s:sx) ptrs.push_back((Event*)&s);
    std::vector<char> buf(sizeof(Events)+ptrs.size()*sizeof(void*)); auto evs=(Events*)buf.data(); evs->numEvents=(int)ptrs.size(); for(size_t i=0;i<ptrs.size();i++) evs->events[i]=ptrs[i];
    fx->dispatcher(fx,effProcessEvents,0,0,evs,0); fx->processReplacing(fx,nullptr,o,512);
    if((pos/512)%8==0) pump();
    if(t1>=shots[si]){ for(int k=0;k<6;k++){ pump(); Sleep(40);} InvalidateRect(ed,nullptr,FALSE); UpdateWindow(ed); pump();
      char fn[256]; sprintf(fn,"%s_%02d.bmp",argv[3],(int)si); Shot(ed,W,H,fn); printf("Screenshot t=%.2fs -> %s\n",shots[si],fn); si++; }
  }
  fx->dispatcher(fx,effEditClose,0,0,0,0); fx->dispatcher(fx,effClose,0,0,0,0); return 0; }
