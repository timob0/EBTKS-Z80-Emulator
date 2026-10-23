#pragma once
#include <stddef.h>
#include <stdio.h>
#include <string.h>

// CP/M 8.3 filenames on HP-85's 32-column CRT. A full field uses eight
// basename columns, an aligned dot, and three extension columns (12).
// Two fields plus six spaces occupy 30 printable positions, leaving a
// two-column margin against automatic wrapping on the physical CRT.
// Pure C++ (no Arduino/heap/guest memory) so exact formatting is testable.
namespace CpmCatalogFormat {
static constexpr size_t kNameWidth=8;
static constexpr size_t kExtensionWidth=3;
static constexpr size_t kFieldWidth=12;
static constexpr size_t kGapWidth=6;
static constexpr size_t kRowWidth=2*kFieldWidth+kGapWidth; // 30 chars
static constexpr size_t kMaxRowBytes=kRowWidth+1; // including ASCII LF

inline void formatField(const char *packed,char (&field)[kFieldWidth+1]) {
  memset(field,' ',kFieldWidth);
  field[kFieldWidth]='\0';
  if(!packed)return;
  // The native BDOS reader already validated the packed name and bounds.
  // Spaces pad the basename on the left of the fixed-position extension.
  const char *dot=strchr(packed,'.');
  size_t baseLen=dot?size_t(dot-packed):strlen(packed);
  if(baseLen>kNameWidth)baseLen=kNameWidth;
  memcpy(field,packed,baseLen);
  if(dot && dot[1]) {
    field[kNameWidth]='.';
    size_t extLen=strlen(dot+1);
    if(extLen>kExtensionWidth)extLen=kExtensionWidth;
    memcpy(field+kNameWidth+1,dot+1,extLen);
  }
}

// Writes one or two names in row-major order. Returns the snprintf length;
// caller must check 0 <= n < capacity. The final row contains one field only.
// Every row terminates with ONE real LF, which HP CPM85 converts to a native
// CRT line break. The directory extent number is intentionally omitted.
inline int formatRow(char *out,size_t capacity,
                     const char *left,const char *right=nullptr) {
  if(!out || capacity==0 || !left)return -1;
  char a[kFieldWidth+1];
  formatField(left,a);
  if(!right)return snprintf(out,capacity,"%s\n",a);
  char b[kFieldWidth+1];
  formatField(right,b);
  return snprintf(out,capacity,"%s      %s\n",a,b);
}
} // namespace CpmCatalogFormat
