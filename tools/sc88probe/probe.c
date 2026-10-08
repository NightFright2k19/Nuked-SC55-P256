// Machbarkeitsprobe SC-88 Pro über die C-Schnittstelle von 88emu (88lib).
#include "c_interface.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static double now(){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec*1e-9; }
static uint32_t msg3(int a,int b,int c){ return (uint32_t)a | ((uint32_t)b<<8) | ((uint32_t)c<<16); }
#define SR 44100
static float buf[2*512];
static double render(emu88_context c, double sec, double* rms_out){
  long n=(long)(sec*SR); double ss=0; long cnt=0;
  while(n>0){ int k=n>512?512:(int)n; emu88_render_float(c,buf,k); for(int i=0;i<k;i++){ss+=buf[2*i]*(double)buf[2*i];} cnt+=k; n-=k; }
  if(rms_out) *rms_out=sqrt(ss/(cnt?cnt:1)); return 0;
}
static emu88_context open_dev(int dev){
  emu88_context c=emu88_create_context();
  if(emu88_select_device(c,dev)!=EMU88_RC_OK){ printf("select_device fehlgeschlagen\n"); exit(1); }
  emu88_set_stereo_output_samplerate(c,SR);
  double t0=now(); int rc=emu88_open_synth(c);
  printf("open_synth rc=%d, Boot %.2f s (echt), Geraete-Rate %u Hz, Ausgabe %u Hz, MIDI-Ports %d\n",rc,now()-t0,emu88_get_device_samplerate(c),emu88_get_actual_stereo_output_samplerate(c),emu88_get_midi_port_count(c));
  if(rc!=EMU88_RC_OK) exit(1);
  return c;
}
static void note_test(emu88_context c,const char* label,int msb,int lsb,int prog){
  emu88_play_msg(c,msg3(0xB2,0,msb)); emu88_play_msg(c,msg3(0xB2,32,lsb)); emu88_play_msg(c,msg3(0xC2,prog,0));
  render(c,0.2,NULL); emu88_play_msg(c,msg3(0x92,60,110)); double r; render(c,0.6,&r);
  emu88_play_msg(c,msg3(0x82,60,0)); render(c,1.5,NULL);
  printf("  %-38s RMS %.4f %s\n",label,r,r>0.003?"HOERBAR":"STILL");
}
static void play_file(int dev,const char* path,double sec,const char* label){
  FILE* f=fopen(path,"r"); if(!f){ printf("(%s fehlt)\n",path); return; }
  emu88_context c=open_dev(dev);
  double t; int n; unsigned b[512]; long pos=0; double tr=0,ss=0; long cnt=0;
  char line[4096]; double t0=now();
  while(fgets(line,sizeof line,f)){
    char* p=line; t=strtod(p,&p); n=(int)strtol(p,&p,10); if(n>512) continue; for(int i=0;i<n;i++) b[i]=strtoul(p,&p,10);
    if(t>sec) break;
    long target=(long)(t*SR); while(pos<target){ int k=target-pos>512?512:(int)(target-pos); emu88_render_float(c,buf,k); for(int i=0;i<k;i++) ss+=buf[2*i]*(double)buf[2*i]; cnt+=k; pos+=k; }
    if(b[0]==0xF0){ uint8_t sx[512]; for(int i=0;i<n;i++) sx[i]=(uint8_t)b[i]; emu88_play_sysex(c,sx,n); }
    else emu88_play_msg(c,msg3(b[0],n>1?b[1]:0,n>2?b[2]:0));
  }
  long end=(long)(sec*SR); while(pos<end){ int k=end-pos>512?512:(int)(end-pos); emu88_render_float(c,buf,k); for(int i=0;i<k;i++) ss+=buf[2*i]*(double)buf[2*i]; cnt+=k; pos+=k; }
  tr=now()-t0; fclose(f);
  printf("  %s: %.0f s Audio in %.1f s -> %.0f %% eines Kerns, RMS %.4f\n",label,sec,tr,100*tr/sec,sqrt(ss/cnt));
  emu88_free_context(c);
}
int main(int argc,char** argv){
  printf("88lib %s\n",emu88_get_library_version_string());
  emu88_add_rom_path(argv[1]);
  int dev=EMU88_DEVICE_SC88PRO;
  printf("SC-88 Pro verfuegbar: %d\n",emu88_is_device_available(dev));
  char d[1024]; emu88_describe_device_roms(dev,d,sizeof d); printf("ROMs: %s\n",d);
  if(!emu88_is_device_available(dev)) return 1;
  emu88_context c=open_dev(dev); render(c,0.5,NULL);
  char txt[256]; emu88_get_display_text(c,0,txt,sizeof txt); printf("Display: \"%s\"\n",txt);
  printf("== CTF (Programm 17, Kanal 3)\n");
  note_test(c,"Bank 0 (Grundklang)",0,0,16);
  note_test(c,"Bank 1 (Variation fehlt -> Fallback?)",1,0,16);
  note_test(c,"Bank 5 (fehlt)",5,0,16);
  printf("== Map-Umschaltung (Bank-LSB / CC32): Programm 1 Piano\n");
  double r[4]; const char* mn[4]={"Standard","SC-55 Map","SC-88 Map","SC-88Pro Map"};
  for(int m=0;m<4;m++){ emu88_play_msg(c,msg3(0xB3,0,0)); emu88_play_msg(c,msg3(0xB3,32,m)); emu88_play_msg(c,msg3(0xC3,0,0)); render(c,0.2,NULL);
    emu88_play_msg(c,msg3(0x93,60,110)); float acc[4]={0}; double ss=0; long cnt=0; long n=(long)(0.5*SR);
    while(n>0){ int k=n>512?512:(int)n; emu88_render_float(c,buf,k); for(int i=0;i<k;i++){ss+=buf[2*i]*(double)buf[2*i]; acc[i&3]+=buf[2*i];} cnt+=k; n-=k; }
    emu88_play_msg(c,msg3(0x83,60,0)); render(c,1.5,NULL); r[m]=sqrt(ss/cnt);
    printf("  %-14s RMS %.5f\n",mn[m],r[m]); }
  emu88_free_context(c);
  printf("== CPU je Einheit (Echtzeitanteil eines Kerns)\n");
  if(argc>2) play_file(dev,argv[2],40,"E1M1 (40 s)");
  if(argc>3) play_file(dev,argv[3],40,"grabbag (40 s)");
  return 0;
}
