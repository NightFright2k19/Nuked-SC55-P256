#include "c_interface.h"
#include <stdio.h>
#include <math.h>
static float buf[2*512];
static void run(emu88_context c,double s){ long n=(long)(s*44100); while(n>0){int k=n>512?512:(int)n; emu88_render_float(c,buf,k); n-=k;} }
static uint32_t m(int a,int b,int d){return a|(b<<8)|(d<<16);}
int main(int argc,char**argv){
  emu88_add_rom_path(argv[1]); emu88_context c=emu88_create_context(); emu88_select_device(c,EMU88_DEVICE_SC88PRO);
  emu88_set_stereo_output_samplerate(c,44100); if(emu88_open_synth(c)) return 1; run(c,0.5);
  printf("Leerlauf: %d Stimmen\n",emu88_get_active_voice_count(c));
  // Voice Reserve aller Parts auf 0? (wie SC-55-Test) - hier Orgel auf Kanal 1-4
  for(int ch=0;ch<4;ch++){ emu88_play_msg(c,m(0xC0|ch,16,0)); emu88_play_msg(c,m(0xB0|ch,7,40)); }
  run(c,0.2);
  for(int k=1;k<=80;k++){ emu88_play_msg(c,m(0x90|(k%4),30+k,90)); run(c,0.02);
    if(k%10==0) printf("%2d Noten gehalten: %d Stimmen klingen\n",k,emu88_get_active_voice_count(c)); }
  for(int k=1;k<=80;k++) emu88_play_msg(c,m(0x80|(k%4),30+k,0));
  for(int i=1;i<=6;i++){ run(c,0.25); printf("+%.2f s nach Loslassen: %d Stimmen\n",0.25*i,emu88_get_active_voice_count(c)); }
  return 0; }
