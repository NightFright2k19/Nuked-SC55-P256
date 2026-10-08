#include "nuked-sc55/common/rom_loader.h"
#include <cstdio>
int main(int,char**a){ common::LoadRomsetResult r{}; common::RomOverrides ov;
 auto e=common::LoadRomset(a[1],a[2],common::RomLoader::Hashing,ov,r);
 printf("Romset '%s' -> err=%d picked=%s\n",a[2],(int)e,r.picked_name.c_str());
 for(size_t i=0;i<ROMLOCATION_COUNT;i++) if(!r.romset_info.rom_paths[i].empty()) printf("  %s\n",r.romset_info.rom_paths[i].string().c_str()); }
