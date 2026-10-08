#include "c_interface.h"
#include <math.h>
#include <stdio.h>
static float buf[2*512];
static uint32_t m(int a,int b,int d){return a|(b<<8)|(d<<16);}
static void run(emu88_context c,double s){ long n=(long)(s*32000); while(n>0){int k=n>371?371:(int)n; emu88_render_float(c,buf,k); n-=k;} }
static double piano(emu88_context c,int ch){ emu88_play_msg(c,m(0x90|ch,60,110)); double ss=0; long cnt=0,n=16000; while(n>0){int k=n>371?371:(int)n; emu88_render_float(c,buf,k); for(int i=0;i<k;i++) ss+=buf[2*i]*(double)buf[2*i]; cnt+=k; n-=k;} emu88_play_msg(c,m(0x80|ch,60,0)); run(c,2); return sqrt(ss/cnt); }
static const char* w(double r){ return fabs(r-0.01355)<0.0006?"SC-55":fabs(r-0.01683)<0.0006?"SC-88":fabs(r-0.00987)<0.0006?"Pro":"?"; }
static const char* ln(unsigned l){ return (l&4)?"SC-55":(l&8)?"SC-88":"Pro"; }
static void press(emu88_context c,unsigned b){ emu88_set_panel_buttons(c,b); run(c,0.15); emu88_set_panel_buttons(c,0); run(c,0.35); }
/* Zustand mitfuehren (Boot = Pro=2). SC-55 MAP: 55<->Pro, 88->55. SC-88 MAP: 88<->Pro, 55->88. */
static int state=2;
static void go(emu88_context c,int t){
  if(t==state) return;
  unsigned key = (t==0) ? (1u<<2) : (t==1) ? (1u<<1) : (state==0 ? (1u<<2) : (1u<<1));
  if(!(emu88_get_panel_leds(c)&1)) press(c,1u<<6);
  press(c,key);
  if(emu88_get_panel_leds(c)&1) press(c,1u<<6);
  state=t;
}
int main(int argc,char**argv){ emu88_add_rom_path(argv[1]); emu88_context c=emu88_create_context(); emu88_select_device(c,EMU88_DEVICE_SC88PRO); emu88_set_stereo_output_samplerate(c,0); emu88_open_synth(c); run(c,0.5);
  emu88_play_msg(c,m(0xC0,0,0)); emu88_play_msg(c,m(0xC3,0,0)); run(c,0.3);
  const char* nm[3]={"SC-55","SC-88","Pro"}; int order[]={1,2,0,1,2,0,2,1,1,0,0,2,2,0,1};
  for(int i=0;i<15;i++){ printf("Ziel %s:\n",nm[order[i]]); go(c,order[i]); printf("  => LED-Map %s, Klang K1 %s, K4 %s\n",ln(emu88_get_panel_leds(c)),w(piano(c,0)),w(piano(c,3))); }
  return 0; }
