#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <FS.h>
#include <mbedtls/base64.h>
#include <ctype.h>
#include <strings.h>
#include "globals.h"
#include "ebtks_storage.h"

// CP/M media manager.
//
// Media files remain on the Teensy SD card under /cpm/disks.  The persistent
// mount table also lives on the Teensy SD card (/cpm/mounts.cfg), while the
// active table is copied into ESP32 RAM at Z80 bootstrap time.  Editing a
// mount therefore never changes media underneath a running CP/M instance;
// the new table takes effect only after a Z80 reset/rebootstrap.

static const uint8_t CPM_DRIVE_COUNT = 16;       // CP/M A: through P:
static const size_t CPM_MEDIA_PATH_MAX = 96;     // keeps 512-byte write JSON < 1024 bytes
static const char *CPM_DISK_ROOT = "/cpm/disks";
static const char *CPM_MOUNT_FILE = "/cpm/mounts.cfg";
static const uint32_t CPM_NATIVE_BYTES = 8UL * 1024UL * 1024UL;
static const uint32_t CPM_NATIVE_SECTORS = CPM_NATIVE_BYTES / 512UL;
static const uint32_t CPM_HP_LOGICAL_BYTES = 256UL * 1024UL;
static const uint32_t CPM_HP_LOGICAL_SECTORS = CPM_HP_LOGICAL_BYTES / 512UL;

// Teensy 4.x SdFat is built with USE_FCNTL_H=1, so it uses the ARM/newlib
// fcntl flag values.  The ordinary writable helper in ebtks_storage.h sends
// only O_RDWR because disk images must never be created or truncated.  The
// mount table is deliberately opened as O_RDWR | O_CREAT | O_TRUNC here.
static const int CPM_TEENSY_O_RDWR  = 0x0002;
static const int CPM_TEENSY_O_CREAT = 0x0200;
static const int CPM_TEENSY_O_TRUNC = 0x0400;
static const int CPM_TEENSY_O_RDWR_CREATE_TRUNC =
    CPM_TEENSY_O_RDWR | CPM_TEENSY_O_CREAT | CPM_TEENSY_O_TRUNC;

enum CpmMediaProfile : uint8_t {
  CPM_MEDIA_NONE = 0,
  CPM_MEDIA_EBTKS8M,
  CPM_MEDIA_HP82900_LIF
};

struct CpmMountEntry {
  bool mounted = false;
  CpmMediaProfile profile = CPM_MEDIA_NONE;
  bool readOnly = true;
  char path[CPM_MEDIA_PATH_MAX] = {};
};

static CpmMountEntry cpmDesiredMounts[CPM_DRIVE_COUNT];
static CpmMountEntry cpmActiveMounts[CPM_DRIVE_COUNT];
static bool cpmDesiredLoaded = false;

inline const char *cpmProfileToken(CpmMediaProfile profile)
{
  switch (profile) {
    case CPM_MEDIA_EBTKS8M: return "ebtks8m";
    case CPM_MEDIA_HP82900_LIF: return "hp82900_lif";
    default: return "none";
  }
}

inline const char *cpmProfileLabel(CpmMediaProfile profile)
{
  switch (profile) {
    case CPM_MEDIA_EBTKS8M: return "EBTKS 8 MB hard disk";
    case CPM_MEDIA_HP82900_LIF: return "HP 82900 CP/M floppy";
    default: return "Unsupported / unknown";
  }
}

inline CpmMediaProfile cpmProfileFromToken(const char *token)
{
  if (!token) return CPM_MEDIA_NONE;
  if (!strcmp(token, "ebtks8m")) return CPM_MEDIA_EBTKS8M;
  if (!strcmp(token, "hp82900_lif")) return CPM_MEDIA_HP82900_LIF;
  return CPM_MEDIA_NONE;
}

inline void cpmClearMount(CpmMountEntry &entry)
{
  entry.mounted = false;
  entry.profile = CPM_MEDIA_NONE;
  entry.readOnly = true;
  entry.path[0] = 0;
}

inline bool cpmValidDrive(int drive)
{
  return drive >= 0 && drive < CPM_DRIVE_COUNT;
}

inline bool cpmPathAllowed(const char *path)
{
  if (!path) return false;
  const size_t rootLen = strlen(CPM_DISK_ROOT);
  if (strncmp(path, CPM_DISK_ROOT, rootLen) != 0 || path[rootLen] != '/') return false;
  if (strstr(path, "..")) return false;
  const size_t len = strlen(path);
  return len > rootLen + 1 && len < CPM_MEDIA_PATH_MAX;
}

inline bool cpmFetchDiskDirectory(JsonDocument &reply)
{
  JsonDocument request;
  request["files"]["path"] = CPM_DISK_ROOT;
  if (!ebtksStorageExchange(request, reply, 8000)) return false;
  return reply["dir"].is<JsonArray>();
}

inline bool cpmLooksLikeHp82900Lif(const char *path)
{
  uint8_t hdr[8] = {};
  if (!ebtksStorageReadRange(path, 0, hdr, sizeof(hdr))) return false;
  return hdr[0] == 0x80 && hdr[1] == 0x00 && !memcmp(&hdr[2], "HPCP/M", 6);
}

inline bool cpmClassifyMedia(const char *path, uint32_t size, bool fsReadOnly,
                             CpmMountEntry &entry)
{
  cpmClearMount(entry);
  if (!cpmPathAllowed(path)) return false;

  if (size == CPM_NATIVE_BYTES) {
    entry.mounted = true;
    entry.profile = CPM_MEDIA_EBTKS8M;
    entry.readOnly = fsReadOnly;
    strlcpy(entry.path, path, sizeof(entry.path));
    return true;
  }

  // HP CP/M LIF disks seen so far are about 616 KiB, but only the first
  // 256 KiB form the CP/M block device.  Identify the format by the LIF magic
  // and the HPCP/M volume label rather than by filename extension.
  if (size >= CPM_HP_LOGICAL_BYTES && size < 2UL * 1024UL * 1024UL &&
      cpmLooksLikeHp82900Lif(path)) {
    entry.mounted = true;
    entry.profile = CPM_MEDIA_HP82900_LIF;
    entry.readOnly = true;                    // first media-manager milestone
    strlcpy(entry.path, path, sizeof(entry.path));
    return true;
  }

  return false;
}

inline bool cpmFindMedia(const char *path, CpmMountEntry &entry, uint32_t *sizeOut = nullptr)
{
  cpmClearMount(entry);
  if (!cpmPathAllowed(path)) return false;

  const char *base = strrchr(path, '/');
  base = base ? base + 1 : path;

  JsonDocument listing;
  if (!cpmFetchDiskDirectory(listing)) return false;

  for (JsonObject item : listing["dir"].as<JsonArray>()) {
    const char *type = item["type"] | "";
    const char *name = item["name"] | "";
    if (strcmp(type, "file") || strcmp(name, base)) continue;

    uint32_t size = item["size"] | 0UL;
    bool ro = item["readonly"] | false;
    if (sizeOut) *sizeOut = size;
    return cpmClassifyMedia(path, size, ro, entry);
  }
  return false;
}

inline bool cpmOpenMountFileForWrite(int &fd)
{
  fd = -1;
  JsonDocument request;
  JsonObject open = request["open"].to<JsonObject>();
  open["path"] = CPM_MOUNT_FILE;
  open["opts"] = CPM_TEENSY_O_RDWR_CREATE_TRUNC;

  JsonDocument reply;
  if (!ebtksStorageExchange(request, reply)) return false;
  fd = reply["open"]["fd"] | -1;
  return fd >= 0;
}

inline bool cpmSaveDesiredMounts()
{
  String text;
  text.reserve(1600);
  text += "# EBTKS CP/M mounts v1\n";
  for (uint8_t i = 0; i < CPM_DRIVE_COUNT; ++i) {
    if (!cpmDesiredMounts[i].mounted) continue;
    text += char('A' + i);
    text += '|';
    text += cpmProfileToken(cpmDesiredMounts[i].profile);
    text += '|';
    text += cpmDesiredMounts[i].readOnly ? "ro" : "rw";
    text += '|';
    text += cpmDesiredMounts[i].path;
    text += '\n';
  }

  int fd = -1;
  if (!cpmOpenMountFileForWrite(fd)) {
    Serial.println("CP/M media: unable to create mount table");
    return false;
  }

  bool ok = true;
  size_t done = 0;
  while (ok && done < text.length()) {
    size_t part = text.length() - done;
    if (part > EBTKS_WRITE_CHUNK) part = EBTKS_WRITE_CHUNK;
    int n = ebtksStorageWriteChunk(fd,
        reinterpret_cast<const uint8_t *>(text.c_str() + done), part);
    if (n != (int)part) ok = false;
    else done += part;
  }

  bool closeOk = ebtksStorageCloseChecked(fd);
  if (!ok || !closeOk) {
    Serial.println("CP/M media: mount table write failed");
    return false;
  }
  return true;
}

inline bool cpmBuildLegacyDefaults()
{
  for (auto &entry : cpmDesiredMounts) cpmClearMount(entry);

  JsonDocument listing;
  if (!cpmFetchDiskDirectory(listing)) return false;

  // Preserve the historical /cpm/disks/A.dsk, B.dsk ... convention on the
  // first boot after upgrading.  Each legacy slot is classified from its
  // actual size/signature, so the existing B.dsk HP LIF experiment migrates
  // automatically to an HP floppy mount.
  for (uint8_t drive = 0; drive < CPM_DRIVE_COUNT; ++drive) {
    char legacyName[8];
    snprintf(legacyName, sizeof(legacyName), "%c.dsk", 'A' + drive);

    for (JsonObject item : listing["dir"].as<JsonArray>()) {
      const char *type = item["type"] | "";
      const char *name = item["name"] | "";
      if (strcmp(type, "file") || strcasecmp(name, legacyName)) continue;

      char path[CPM_MEDIA_PATH_MAX];
      snprintf(path, sizeof(path), "%s/%s", CPM_DISK_ROOT, name);
      uint32_t size = item["size"] | 0UL;
      bool ro = item["readonly"] | false;
      cpmClassifyMedia(path, size, ro, cpmDesiredMounts[drive]);
      break;
    }
  }

  cpmDesiredLoaded = true;
  if (!cpmSaveDesiredMounts()) {
    // Persistence is convenient, but it must never make a firmware upgrade
    // lose the legacy A.dsk/B.dsk media for the current boot.
    Serial.println("CP/M media: legacy mounts imported in RAM; mount table persistence failed");
  }
  return true;
}

inline bool cpmLoadDesiredMounts(bool createDefaults = true)
{
  for (auto &entry : cpmDesiredMounts) cpmClearMount(entry);

  char text[2048];
  size_t len = 0;
  if (!ebtksStorageReadText(CPM_MOUNT_FILE, text, sizeof(text), len)) {
    if (!createDefaults) return false;
    Serial.println("CP/M media: no mount table yet; importing legacy drive filenames");
    return cpmBuildLegacyDefaults();
  }

  char *saveLine = nullptr;
  for (char *line = strtok_r(text, "\n", &saveLine);
       line;
       line = strtok_r(nullptr, "\n", &saveLine)) {
    while (*line == ' ' || *line == '\t' || *line == '\r') ++line;
    size_t ll = strlen(line);
    while (ll && (line[ll - 1] == '\r' || line[ll - 1] == ' ' || line[ll - 1] == '\t'))
      line[--ll] = 0;
    if (!line[0] || line[0] == '#') continue;

    char *saveField = nullptr;
    char *driveText = strtok_r(line, "|", &saveField);
    char *profileText = strtok_r(nullptr, "|", &saveField);
    char *accessText = strtok_r(nullptr, "|", &saveField);
    char *pathText = strtok_r(nullptr, "|", &saveField);
    if (!driveText || !profileText || !accessText || !pathText) continue;

    int drive = toupper((unsigned char)driveText[0]) - 'A';
    CpmMediaProfile profile = cpmProfileFromToken(profileText);
    if (!cpmValidDrive(drive) || profile == CPM_MEDIA_NONE || !cpmPathAllowed(pathText)) continue;

    CpmMountEntry &entry = cpmDesiredMounts[drive];
    entry.mounted = true;
    entry.profile = profile;
    entry.readOnly = !strcasecmp(accessText, "ro") || profile == CPM_MEDIA_HP82900_LIF;
    strlcpy(entry.path, pathText, sizeof(entry.path));
  }

  cpmDesiredLoaded = true;
  return true;
}

inline bool cpmMountDesired(uint8_t drive, const char *path)
{
  if (!cpmValidDrive(drive)) return false;
  CpmMountEntry media;
  if (!cpmFindMedia(path, media)) return false;

  if (!cpmDesiredLoaded && !cpmLoadDesiredMounts()) return false;
  cpmDesiredMounts[drive] = media;
  return cpmSaveDesiredMounts();
}

inline bool cpmUnmountDesired(uint8_t drive)
{
  if (!cpmValidDrive(drive)) return false;
  if (!cpmDesiredLoaded && !cpmLoadDesiredMounts()) return false;
  cpmClearMount(cpmDesiredMounts[drive]);
  return cpmSaveDesiredMounts();
}

inline uint16_t cpmRamWord(uint16_t address)
{
  return (uint16_t)RAM[address] | ((uint16_t)RAM[address + 1] << 8);
}

inline void cpmSetRamWord(uint16_t address, uint16_t value)
{
  RAM[address] = (uint8_t)(value & 0xff);
  RAM[address + 1] = (uint8_t)(value >> 8);
}

inline bool cpmApplyBiosGeometry()
{
  // Layout of the currently supplied CBIOS: 16 consecutive 16-byte DPHs
  // start at E633h.  All native entries point at the common DPB immediately
  // after the DPH table (E733h).  Verify rather than silently patching a
  // different BIOS.
  static const uint16_t DPH_BASE = 0xE633;
  static const uint16_t DPH_SIZE = 16;
  static const uint16_t DPH_DPB_OFFSET = 10;
  static const uint16_t DPH_CSV_OFFSET = 12;
  static const uint16_t DPH_ALV_OFFSET = 14;

  uint16_t nativeDpb = cpmRamWord(DPH_BASE + DPH_DPB_OFFSET);
  if (nativeDpb < 0xE600 || nativeDpb > 0xF000 ||
      RAM[nativeDpb] != 0x80 || RAM[nativeDpb + 1] != 0x00 ||
      RAM[nativeDpb + 2] != 0x05 || RAM[nativeDpb + 3] != 0x1F) {
    Serial.println("CP/M media: CBIOS DPH/native-DPB layout check failed");
    return false;
  }

  static const uint8_t hpDpb[] = {
    0x20, 0x00,             // SPT  = 32
    0x03,                   // BSH  = 3
    0x07,                   // BLM  = 7
    0x00,                   // EXM  = 0
    0xF3, 0x00,             // DSM  = 243
    0x7F, 0x00,             // DRM  = 127
    0xF0,                   // AL0
    0x00,                   // AL1
    0x20, 0x00,             // CKS  = 32
    0x03, 0x00              // OFF  = 3
  };

  for (uint8_t drive = 0; drive < CPM_DRIVE_COUNT; ++drive) {
    uint16_t dph = DPH_BASE + drive * DPH_SIZE;

    // Every bootstrap reloads cbios.rom, nevertheless restore the native
    // fields explicitly so switching a drive back from HP to native is clear.
    cpmSetRamWord(dph + DPH_DPB_OFFSET, nativeDpb);
    cpmSetRamWord(dph + DPH_CSV_OFFSET, 0);

    if (!cpmActiveMounts[drive].mounted ||
        cpmActiveMounts[drive].profile != CPM_MEDIA_HP82900_LIF) continue;

    // A native allocation vector is 257 bytes.  An HP floppy needs only 31
    // allocation-vector bytes, leaving ample private space in that drive's
    // existing ALV buffer for its 15-byte DPB and 32-byte CSV.  This avoids
    // consuming global BIOS RAM and works independently for multiple HP drives.
    uint16_t alv = cpmRamWord(dph + DPH_ALV_OFFSET);
    uint32_t hpDpbAddr32 = (uint32_t)alv + 0x40;
    uint32_t csvAddr32 = (uint32_t)alv + 0x60;
    if (alv < 0xE000 || csvAddr32 + 32 > 0x10000UL) {
      Serial.printf("CP/M media: %c: invalid ALV address; leaving drive unmounted\n", 'A' + drive);
      cpmClearMount(cpmActiveMounts[drive]);
      continue;
    }

    uint16_t hpDpbAddr = (uint16_t)hpDpbAddr32;
    uint16_t csvAddr = (uint16_t)csvAddr32;
    memcpy(&RAM[hpDpbAddr], hpDpb, sizeof(hpDpb));
    memset(&RAM[csvAddr], 0, 32);
    cpmSetRamWord(dph + DPH_DPB_OFFSET, hpDpbAddr);
    cpmSetRamWord(dph + DPH_CSV_OFFSET, csvAddr);
  }
  return true;
}

inline bool cpmActivateMounts()
{
  cpmDesiredLoaded = false;
  if (!cpmLoadDesiredMounts()) {
    Serial.println("CP/M media: unable to load mount table; no virtual media activated");
    for (auto &entry : cpmActiveMounts) cpmClearMount(entry);
    return false;
  }

  memcpy(cpmActiveMounts, cpmDesiredMounts, sizeof(cpmActiveMounts));
  bool geometryOk = cpmApplyBiosGeometry();

  Serial.println("CP/M media mounts:");
  for (uint8_t drive = 0; drive < CPM_DRIVE_COUNT; ++drive) {
    const CpmMountEntry &entry = cpmActiveMounts[drive];
    if (!entry.mounted) continue;
    Serial.printf("  %c: %-22s %s [%s]\n", 'A' + drive,
                  cpmProfileLabel(entry.profile), entry.path,
                  entry.readOnly ? "R/O" : "R/W");
  }
  return geometryOk;
}

inline bool cpmMapMountedSector(uint8_t drive, uint32_t biosSector, uint32_t &storageSector)
{
  if (!cpmValidDrive(drive)) return false;
  const CpmMountEntry &entry = cpmActiveMounts[drive];
  if (!entry.mounted) return false;

  if (entry.profile == CPM_MEDIA_EBTKS8M) {
    if (biosSector >= CPM_NATIVE_SECTORS) return false;
    storageSector = biosSector;
    return true;
  }

  if (entry.profile == CPM_MEDIA_HP82900_LIF) {
    // The stock CBIOS LBA encoder always uses 32 host sectors/track.  With the
    // HP DPB only host sectors 0..7 can occur, so compact track*32+sector into
    // the raw LIF block device's track*8+sector numbering.
    uint32_t track = biosSector >> 5;
    uint32_t hostSector = biosSector & 0x1f;
    if (hostSector >= 8) return false;
    storageSector = (track << 3) | hostSector;
    return storageSector < CPM_HP_LOGICAL_SECTORS;
  }

  return false;
}

inline bool cpmMountedDiskRead(uint8_t drive, uint32_t storageSector, uint8_t *dst)
{
  if (!dst || !cpmValidDrive(drive) || !cpmActiveMounts[drive].mounted) return false;

  const CpmMountEntry &mount = cpmActiveMounts[drive];
  // Optional SPI read-ahead. Never copy partial/corrupt SPI data into Z80
  // RAM. On any failure the SPI service disables itself for this boot and
  // the unchanged, known-good UART/Base64 sector read runs below.
  if (spiDiskReadsEnabled) {
    const uint32_t sectors =
        mount.profile == CPM_MEDIA_EBTKS8M ? CPM_NATIVE_SECTORS :
        mount.profile == CPM_MEDIA_HP82900_LIF ? CPM_HP_LOGICAL_SECTORS : 0;
    if (sectors && spiDiskReadCached(mount.path, storageSector, sectors, dst))
      return true;
    ++spiDiskFallbacks;
    z80DebugLog("CP/M SPI read fallback: %c: sector=%lu reason=%s\n",
                (char)('A' + drive), (unsigned long)storageSector,
                spiDiskLastError);
  }

  JsonDocument request;
  JsonObject disk = request["disk_read"].to<JsonObject>();
  disk["drive"] = drive;                 // retained for diagnostics/backward compatibility
  disk["sector"] = storageSector;
  disk["path"] = cpmActiveMounts[drive].path;

  JsonDocument reply;
  if (!ebtksStorageExchange(request, reply)) return false;
  int reported = reply["disk_read"]["len"] | -1;
  if (reported != (int)EBTKS_DISK_SECTOR_SIZE) return false;

  const char *encoded = reply["disk_read"]["data"] | "";
  size_t decoded = 0;
  int rc = mbedtls_base64_decode(dst, EBTKS_DISK_SECTOR_SIZE, &decoded,
      reinterpret_cast<const unsigned char *>(encoded), strlen(encoded));
  return rc == 0 && decoded == EBTKS_DISK_SECTOR_SIZE;
}

inline bool cpmMountedDiskWrite(uint8_t drive, uint32_t storageSector, const uint8_t *src)
{
  // CRITICAL: even an unsuccessful write must not leave any stale prefetched
  // sector data to be served later. All actual writes remain on UART.
  spiDiskInvalidateCache();
  if (!src || !cpmValidDrive(drive)) return false;
  const CpmMountEntry &entry = cpmActiveMounts[drive];
  if (!entry.mounted || entry.readOnly) return false;

  unsigned char encoded[4 * ((EBTKS_DISK_SECTOR_SIZE + 2) / 3) + 1];
  size_t encodedLen = 0;
  int rc = mbedtls_base64_encode(encoded, sizeof(encoded), &encodedLen,
      src, EBTKS_DISK_SECTOR_SIZE);
  if (rc != 0) return false;
  encoded[encodedLen] = 0;

  JsonDocument request;
  JsonObject disk = request["disk_write"].to<JsonObject>();
  disk["drive"] = drive;
  disk["sector"] = storageSector;
  disk["path"] = entry.path;
  disk["data"] = reinterpret_cast<const char *>(encoded);

  JsonDocument reply;
  if (!ebtksStorageExchange(request, reply)) return false;
  int written = reply["disk_write"]["len"] | -1;
  return written == (int)EBTKS_DISK_SECTOR_SIZE;
}

inline bool cpmMountTablesDiffer()
{
  for (uint8_t i = 0; i < CPM_DRIVE_COUNT; ++i) {
    const CpmMountEntry &a = cpmDesiredMounts[i];
    const CpmMountEntry &b = cpmActiveMounts[i];
    if (a.mounted != b.mounted || a.profile != b.profile || a.readOnly != b.readOnly ||
        strcmp(a.path, b.path)) return true;
  }
  return false;
}

inline void cpmAddMountJson(JsonObject obj, const CpmMountEntry &entry)
{
  obj["mounted"] = entry.mounted;
  if (!entry.mounted) return;
  obj["path"] = entry.path;
  obj["profile"] = cpmProfileToken(entry.profile);
  obj["label"] = cpmProfileLabel(entry.profile);
  obj["readonly"] = entry.readOnly;
}

inline bool cpmBuildMediaStatusJson(String &out)
{
  if (!cpmLoadDesiredMounts()) return false;

  JsonDocument result;
  result["ok"] = true;
  result["pending"] = cpmMountTablesDiffer();
  JsonArray drives = result["drives"].to<JsonArray>();
  for (uint8_t i = 0; i < CPM_DRIVE_COUNT; ++i) {
    JsonObject d = drives.add<JsonObject>();
    char driveText[2] = {(char)('A' + i), 0};
    d["drive"] = driveText;
    cpmAddMountJson(d["desired"].to<JsonObject>(), cpmDesiredMounts[i]);
    cpmAddMountJson(d["active"].to<JsonObject>(), cpmActiveMounts[i]);
  }

  JsonDocument listing;
  bool listingOk = cpmFetchDiskDirectory(listing);
  result["media_ok"] = listingOk;
  JsonArray media = result["media"].to<JsonArray>();
  if (listingOk) {
    for (JsonObject item : listing["dir"].as<JsonArray>()) {
      const char *type = item["type"] | "";
      const char *name = item["name"] | "";
      // Ignore macOS .DS_Store, ._AppleDouble and other dot-prefixed files
      // before media classification/JSON allocation: no disk probe, mount
      // suggestion, or UI entry for hidden metadata.
      if (strcmp(type, "file") || !name[0] || name[0] == '.') continue;

      char path[CPM_MEDIA_PATH_MAX];
      int n = snprintf(path, sizeof(path), "%s/%s", CPM_DISK_ROOT, name);
      if (n <= 0 || n >= (int)sizeof(path)) continue;

      uint32_t size = item["size"] | 0UL;
      bool fsRo = item["readonly"] | false;
      CpmMountEntry classified;
      bool supported = cpmClassifyMedia(path, size, fsRo, classified);

      JsonObject m = media.add<JsonObject>();
      m["name"] = name;
      m["path"] = path;
      m["size"] = size;
      m["supported"] = supported;
      m["profile"] = supported ? cpmProfileToken(classified.profile) : "unknown";
      m["label"] = supported ? cpmProfileLabel(classified.profile) : "Unsupported / unknown";
      m["readonly"] = supported ? classified.readOnly : fsRo;

      String mountedOn;
      for (uint8_t drive = 0; drive < CPM_DRIVE_COUNT; ++drive) {
        if (cpmDesiredMounts[drive].mounted && !strcmp(cpmDesiredMounts[drive].path, path)) {
          if (mountedOn.length()) mountedOn += ',';
          mountedOn += char('A' + drive);
          mountedOn += ':';
        }
      }
      m["mounted_on"] = mountedOn;
    }
  }

  out = "";
  serializeJson(result, out);
  return true;
}
