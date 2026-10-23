// Host-only CP/M 8.3 catalog layout regression for the 32-column HP-85.
// No Arduino or guest; test the EXACT formatter used by ESP32 firmware.
#include "../ESP32_Z80_Emulator/cpm_catalog_format.h"
#include <cassert>
#include <cstdio>
#include <cstring>

static void testFixedEightDotThreeColumns() {
  char field[13]={};
  CpmCatalogFormat::formatField("STAT.COM",field);
  assert(strcmp(field,"STAT    .COM")==0);
  CpmCatalogFormat::formatField("PIP.COM",field);
  assert(strcmp(field,"PIP     .COM")==0);
  CpmCatalogFormat::formatField("DOWNLOAD.COM",field);
  assert(strcmp(field,"DOWNLOAD.COM")==0);
  CpmCatalogFormat::formatField("MP80",field);
  assert(strcmp(field,"MP80        ")==0);
  CpmCatalogFormat::formatField("A.X",field);
  assert(strcmp(field,"A       .X  ")==0);
  CpmCatalogFormat::formatField("INSTALL.DAT",field);
  assert(strcmp(field,"INSTALL .DAT")==0);
}

static void testTwoColumnsSafeOn32ColumnCrt() {
  static_assert(CpmCatalogFormat::kRowWidth==30,"HP-85 row must have margin");
  static_assert(CpmCatalogFormat::kMaxRowBytes==31,"30 visible + LF");
  char row[64]={};
  int n=CpmCatalogFormat::formatRow(row,sizeof(row),"STAT.COM","PIP.COM");
  assert(n==31);
  assert(strcmp(row,"STAT    .COM      PIP     .COM\n")==0);
  assert(row[8]=='.' && row[26]=='.');
  assert(row[30]=='\n' && row[31]=='\0');
  assert(!strstr(row,"\\n")); // no printable backslash+n
  assert(strchr(row,'\r')==nullptr);

  // Last odd entry must occupy left column only.
  n=CpmCatalogFormat::formatRow(row,sizeof(row),"MP80");
  assert(n==13);
  assert(strcmp(row,"MP80        \n")==0);

  // All 32 result slots must fit inside 680-byte HP/Teensy mailbox
  // alongside header and warning/truncation footers.
  char catalog[680]={};
  size_t used=0;
  n=snprintf(catalog,sizeof(catalog),"CP/M A: DIRECTORY\n");
  assert(n>0);
  used=(size_t)n;
  for(unsigned i=0;i<32;i+=2) {
    n=CpmCatalogFormat::formatRow(
        catalog+used,sizeof(catalog)-used,"DOWNLOAD.COM","MP80");
    assert(n==31 && (size_t)n<sizeof(catalog)-used);
    used+=(size_t)n;
  }
  n=snprintf(catalog+used,sizeof(catalog)-used,
             "30 EXTENTS (MORE NOT SHOWN)\n2 BAD NAME(S) SKIPPED\n");
  assert(n>0 && (size_t)n<sizeof(catalog)-used);
  used+=(size_t)n;
  assert(used<680);
  // Header/footer may be shorter; directory rows must remain <=30 chars.
  const char *p=strchr(catalog,'\n');
  assert(p!=nullptr);
  ++p;
  for(unsigned i=0;i<16;++i) {
    const char *nl=strchr(p,'\n');
    assert(nl && (size_t)(nl-p)==30);
    p=nl+1;
  }
}

static void testBounds() {
  char shortRow[13]={};
  assert(CpmCatalogFormat::formatRow(shortRow,sizeof(shortRow),
                                     "STAT.COM","PIP.COM")==31);
  assert(shortRow[sizeof(shortRow)-1]=='\0');
  assert(CpmCatalogFormat::formatRow(nullptr,0,"STAT.COM","PIP.COM")==-1);
}

int main() {
  testFixedEightDotThreeColumns();
  testTwoColumnsSafeOn32ColumnCrt();
  testBounds();
  puts("PASS: HP CPDCAT aligned 8.3 fields, two-column rows, CRT width, LF and 680-byte cap");
}
