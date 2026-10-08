#include <cstdio>
extern unsigned long long g_cnt[8];
struct P{ ~P(){ printf("Sync durch Clock-Ereignis %llu | TIMER_Read %llu | Read2 %llu | Write %llu | Write2 %llu\n",g_cnt[0],g_cnt[1],g_cnt[2],g_cnt[3],g_cnt[4]); } } p;
