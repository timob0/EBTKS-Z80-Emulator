#include <cassert>
#include <cstdint>
#include <cstring>
#include "../ESP32_Z80_Emulator/spi_cache_words.h"

// Native regression covers all 16 cache sectors and byte packing boundaries.
// It cannot prove ESP32 instruction RAM capability; hardware is needed.
int main() {
  alignas(4) uint32_t cache[16*EbtksSpiCache::kSectorWords]={};
  uint8_t src[512],dst[512];
  for(size_t sector=0;sector<16;++sector) {
    for(size_t p=0;p<512;++p)
      src[p]=(uint8_t)((p*173u+sector*57u+(p>>2))&255u);
    EbtksSpiCache::store(cache+sector*EbtksSpiCache::kSectorWords,src);
  }
  for(size_t sector=0;sector<16;++sector) {
    for(size_t p=0;p<512;++p)
      src[p]=(uint8_t)((p*173u+sector*57u+(p>>2))&255u);
    memset(dst,0x5A,sizeof(dst));
    EbtksSpiCache::load(dst,cache+sector*EbtksSpiCache::kSectorWords);
    assert(memcmp(dst,src,sizeof(dst))==0);
  }
  // Replacing one sector must not affect its neighbors or lose 0x00/0xFF.
  memset(src,0,sizeof(src));
  src[0]=0xFF;src[3]=0xFF;src[507]=0xFF;src[511]=0xFF;
  EbtksSpiCache::store(cache+7*EbtksSpiCache::kSectorWords,src);
  EbtksSpiCache::load(dst,cache+7*EbtksSpiCache::kSectorWords);
  assert(memcmp(src,dst,512)==0);
  EbtksSpiCache::load(dst,cache+6*EbtksSpiCache::kSectorWords);
  for(size_t p=0;p<512;++p)
    assert(dst[p]==(uint8_t)((p*173u+6*57u+(p>>2))&255u));
}
