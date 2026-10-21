#pragma once
#include <stddef.h>
#include <stdint.h>

// Portable, byte-exact sector transfer between ordinary DRAM and 32-bit-only
// memory (on classic ESP32, instruction RAM can require aligned word access).
// NEVER pass the words pointer to memcpy/memset or cast it to a uint8_t pointer.
// Caller guarantees 4-byte alignment and sufficient 32-bit words.
namespace EbtksSpiCache {
static constexpr size_t kSectorBytes = 512;
static constexpr size_t kSectorWords = kSectorBytes / sizeof(uint32_t);

inline void store(uint32_t* words, const uint8_t* src) {
  for(size_t i=0; i<kSectorWords; ++i) {
    const size_t p=i*4;
    const uint32_t value=
        (uint32_t)src[p] | ((uint32_t)src[p+1] << 8)
        | ((uint32_t)src[p+2] << 16) | ((uint32_t)src[p+3] << 24);
    words[i]=value; // exactly one aligned 32-bit IRAM store
  }
}
inline void load(uint8_t* dst, const uint32_t* words) {
  for(size_t i=0; i<kSectorWords; ++i) {
    const uint32_t value=words[i]; // exactly one aligned 32-bit IRAM load
    const size_t p=i*4;
    dst[p]=(uint8_t)value;
    dst[p+1]=(uint8_t)(value>>8);
    dst[p+2]=(uint8_t)(value>>16);
    dst[p+3]=(uint8_t)(value>>24);
  }
}
} // namespace EbtksSpiCache
