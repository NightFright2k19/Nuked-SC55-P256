#include "nuked-sc55/common/rom_loader.h"
#include <cstdio>
int main(int,char**a){ common::LoadRomsetResult r{}; common::RomOverrides ov;
 common::LoadRomset(a[1],a[2],common::RomLoader::Hashing,ov,r);
 printf("romset enum %d\n",(int)r.romset);
 for(size_t i=0;i<ROMLOCATION_COUNT;i++) if(!r.romset_info.rom_paths[i].empty()) printf("  loc %zu: %s (%zu bytes)\n",i,r.romset_info.rom_paths[i].filename().string().c_str(),r.romset_info.rom_data[i].size()); }
