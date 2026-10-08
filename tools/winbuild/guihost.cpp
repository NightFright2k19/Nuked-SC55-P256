// VST2-GUI-Testhost: lädt Plugin, öffnet Editor, spielt MIDI, speichert Screenshots (BMP).
#include <windows.h>
#include "vst2/vst2_abi.h"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace vst2;
static int g_upd=0; static intptr_t cb(AEffect*,int32_t op,int32_t,intptr_t,void*,float){ if(op==42) g_upd++; return op==audioMasterVersion?2400:0; }
static void SaveShot(HWND w,int W,int H,const char* fn){
  HDC wdc=GetDC(w); HDC mem=CreateCompatibleDC(wdc);
  BITMAPINFO bi{}; bi.bmiHeader.biSize=sizeof(bi.bmiHeader); bi.bmiHeader.biWidth=W; bi.bmiHeader.biHeight=-H; bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32;
  void* bits=nullptr; HBITMAP bm=CreateDIBSection(mem,&bi,DIB_RGB_COLORS,&bits,nullptr,0); SelectObject(mem,bm);
  PrintWindow(w,mem,PW_CLIENTONLY); BitBlt(mem,0,0,W,H,wdc,0,0,SRCCOPY); GdiFlush();
  FILE* f=fopen(fn,"wb"); BITMAPFILEHEADER fh{0x4D42,(DWORD)(54+W*H*4),0,0,54}; BITMAPINFOHEADER ih=bi.bmiHeader; ih.biHeight=-H;
  fwrite(&fh,sizeof fh,1,f); fwrite(&ih,sizeof ih,1,f); fwrite(bits,4,W*H,f); fclose(f);
  DeleteObject(bm); DeleteDC(mem); ReleaseDC(w,wdc); }
static HWND g_ed; static int g_W,g_H;
static DWORD WINAPI MenuThread(void*){
  Sleep(400);
  // SETUP-Button anklicken (rechts unten im Editor)
  PostMessage(g_ed,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(g_W-80,g_H-50));
  Sleep(900);
  // Bildschirm-Screenshot inkl. Menü
  HDC sdc=GetDC(nullptr); int W=760,H=560; HDC mem=CreateCompatibleDC(sdc);
  BITMAPINFO bi{}; bi.bmiHeader.biSize=sizeof(bi.bmiHeader); bi.bmiHeader.biWidth=W; bi.bmiHeader.biHeight=-H; bi.bmiHeader.biPlanes=1; bi.bmiHeader.biBitCount=32;
  void* bits=nullptr; HBITMAP bm=CreateDIBSection(mem,&bi,DIB_RGB_COLORS,&bits,nullptr,0); SelectObject(mem,bm);
  BitBlt(mem,0,0,W,H,sdc,0,0,SRCCOPY); GdiFlush();
  FILE* f=fopen("Z:\\tmp\\menu.bmp","wb"); BITMAPFILEHEADER fh{0x4D42,(DWORD)(54+W*H*4),0,0,54}; BITMAPINFOHEADER ih=bi.bmiHeader;
  fwrite(&fh,sizeof fh,1,f); fwrite(&ih,sizeof ih,1,f); fwrite(bits,4,W*H,f); fclose(f);
  // Eintrag "64 Stimmen" wählen: 3x Pfeil runter (Kopfzeile ist deaktiviert) + Enter
  for(int i=0;i<3;i++){ keybd_event(VK_DOWN,0,0,0); keybd_event(VK_DOWN,0,KEYEVENTF_KEYUP,0); Sleep(80);} 
  keybd_event(VK_RETURN,0,0,0); keybd_event(VK_RETURN,0,KEYEVENTF_KEYUP,0);
  return 0; }
static void pump(){ MSG m; while(PeekMessage(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessage(&m);} }
int main(int,char**a){
  HMODULE h=LoadLibraryA(a[1]); auto mainf=(AEffect*(*)(HostCallback))GetProcAddress(h,"VSTPluginMain");
  AEffect* fx=mainf(cb); if(!fx){puts("fail");return 1;}
  printf("HasEditor=%d ProgramChunks=%d\n",!!(fx->flags&FlagHasEditor),!!(fx->flags&FlagProgramChunks));
  fx->dispatcher(fx,effOpen,0,0,0,0); fx->dispatcher(fx,effSetSampleRate,0,0,0,44100.f); fx->dispatcher(fx,effSetBlockSize,0,512,0,0); fx->dispatcher(fx,effMainsChanged,0,1,0,0);
  ERect* r=nullptr; fx->dispatcher(fx,effEditGetRect,0,0,&r,0); int W=r->right-r->left,H=r->bottom-r->top; printf("Editor %dx%d\n",W,H);
  WNDCLASSA wc{}; wc.lpfnWndProc=DefWindowProcA; wc.hInstance=GetModuleHandle(0); wc.lpszClassName="HostWin"; RegisterClassA(&wc);
  RECT rc{0,0,W,H}; AdjustWindowRect(&rc,WS_OVERLAPPEDWINDOW,FALSE);
  HWND host=CreateWindowA("HostWin","Test",WS_OVERLAPPEDWINDOW|WS_VISIBLE,0,0,rc.right-rc.left,rc.bottom-rc.top,0,0,wc.hInstance,0);
  printf("EditOpen=%d\n",(int)fx->dispatcher(fx,effEditOpen,0,0,host,0));
  HWND ed=GetWindow(host,GW_CHILD); g_ed=ed; g_W=W; g_H=H;
  { void* o0=nullptr; int n0=(int)fx->dispatcher(fx,effGetChunk,0,0,&o0,0); printf("Chunk frische Instanz: %.*s\n",n0,(char*)o0); }
  // State chunk round-trip
  const char* ch="NSC55P1 max_voices=128"; fx->dispatcher(fx,effSetChunk,0,(intptr_t)strlen(ch),(void*)ch,0);
  void* out=nullptr; int n=(int)fx->dispatcher(fx,effGetChunk,0,0,&out,0); printf("Chunk: %.*s\n",n,(char*)out);
  std::vector<float> L(512),R(512); float* o[2]={L.data(),R.data()};
  int progs[16]={0,48,33,25,61,73,52,89,19,0,40,0,0,0,0,0};
  for(int b=0;b<260;b++){
    std::vector<MidiEvent> me; auto add=[&](uint8_t x,uint8_t y,uint8_t z){MidiEvent m{};m.type=kMidiType;m.byteSize=sizeof m;m.midiData[0]=x;m.midiData[1]=y;m.midiData[2]=z;me.push_back(m);};
    if(b==1) for(int c=0;c<16;c++){ add(0xC0|c,progs[c],0); add(0xB0|c,7,90+c); add(0xB0|c,10,c*8); add(0xB0|c,91,40+c*3); add(0xB0|c,93,c*5); }
    if(b>=5 && b%6==0) for(int c=0;c<16;c++) if((b/6+c)%3!=0) add(0x90|c,36+(c*5+b)%48,40+((c*13+b*7)%87));
    if(b==150) for(int c=0;c<6;c++) for(int k=0;k<10;k++) add(0x90|c,40+k*3+c,100); // Akkordflut -> mehr Stimmen
    std::vector<Event*> ptrs; for(auto&m:me) ptrs.push_back((Event*)&m);
    std::vector<char> buf(sizeof(Events)+ptrs.size()*sizeof(void*)); auto ev=(Events*)buf.data(); ev->numEvents=ptrs.size(); for(size_t i=0;i<ptrs.size();i++) ev->events[i]=ptrs[i];
    fx->dispatcher(fx,effProcessEvents,0,0,ev,0); fx->processReplacing(fx,nullptr,o,512);
    pump(); Sleep(5);
    if(b==120) SaveShot(ed,W,H,"Z:\\tmp\\shot1.bmp");
    if(b==158) SaveShot(ed,W,H,"Z:\\tmp\\shot2.bmp");
    if(b==170) CreateThread(nullptr,0,MenuThread,nullptr,0,nullptr);
    if(b==240){ void* o2=nullptr; int n2=(int)fx->dispatcher(fx,effGetChunk,0,0,&o2,0); printf("Chunk nach Menüwahl: %.*s | Host-Änderungsmeldungen: %d\n",n2,(char*)o2,g_upd); SaveShot(ed,W,H,"Z:\\tmp\\shot3.bmp"); }
  }
  fx->dispatcher(fx,effEditClose,0,0,0,0); DestroyWindow(host); fx->dispatcher(fx,effClose,0,0,0,0); puts("ok"); return 0;
}
