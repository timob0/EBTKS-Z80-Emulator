#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <strings.h>
#include "cpm_media.h"

// Reliable persistent mount store.
//
// Disk images remain exclusively on the Teensy SD card under /cpm/disks.
// The tiny A:-P: assignment table is stored in ESP32 NVS so a Z80 reset does
// not depend on creating/updating a configuration file over the UART link.
// /cpm/mounts.cfg is still accepted once as a migration source.

static const char *CPM_PREF_NAMESPACE = "cpmmount";
static const uint32_t CPM_PREF_MAGIC = 0x43504D31UL; // "CPM1"
static bool cpmStoreLoaded = false;

inline bool cpmMountEntryEqual(const CpmMountEntry &a, const CpmMountEntry &b)
{
  return a.mounted == b.mounted &&
         a.profile == b.profile &&
         a.readOnly == b.readOnly &&
         !strcmp(a.path, b.path);
}

inline bool cpmMountArrayEqual(const CpmMountEntry *a, const CpmMountEntry *b)
{
  for (uint8_t i = 0; i < CPM_DRIVE_COUNT; ++i) {
    if (!cpmMountEntryEqual(a[i], b[i])) return false;
  }
  return true;
}

inline bool cpmStoreReadPrefs(CpmMountEntry *out, bool &found)
{
  found = false;
  if (!out) return false;
  for (uint8_t i = 0; i < CPM_DRIVE_COUNT; ++i) cpmClearMount(out[i]);

  Preferences prefs;
  // A read-only open fails when the namespace has never been created. That is
  // the normal first-boot state, not an NVS error: report an empty/not-found
  // table and let cpmStoreLoadDesired() initialize it through the R/W path.
  if (!prefs.begin(CPM_PREF_NAMESPACE, true)) return true;
  uint32_t magic = prefs.getUInt("magic", 0);
  if (magic != CPM_PREF_MAGIC) {
    prefs.end();
    return true;
  }

  found = true;
  for (uint8_t i = 0; i < CPM_DRIVE_COUNT; ++i) {
    char key[4];
    snprintf(key, sizeof(key), "d%c", 'A' + i);
    String value = prefs.getString(key, "");
    if (!value.length()) continue;

    char temp[CPM_MEDIA_PATH_MAX + 32];
    strlcpy(temp, value.c_str(), sizeof(temp));
    char *save = nullptr;
    char *profileText = strtok_r(temp, "|", &save);
    char *accessText = strtok_r(nullptr, "|", &save);
    char *pathText = strtok_r(nullptr, "|", &save);
    if (!profileText || !accessText || !pathText) continue;

    CpmMediaProfile profile = cpmProfileFromToken(profileText);
    if (profile == CPM_MEDIA_NONE || !cpmPathAllowed(pathText)) continue;

    CpmMountEntry &entry = out[i];
    entry.mounted = true;
    entry.profile = profile;
    entry.readOnly = !strcasecmp(accessText, "ro") || profile == CPM_MEDIA_HP82900_LIF;
    strlcpy(entry.path, pathText, sizeof(entry.path));
  }
  prefs.end();
  return true;
}

inline bool cpmStoreWritePrefs(const CpmMountEntry *entries)
{
  if (!entries) return false;

  Preferences prefs;
  if (!prefs.begin(CPM_PREF_NAMESPACE, false)) return false;
  if (!prefs.clear()) {
    prefs.end();
    return false;
  }

  if (prefs.putUInt("magic", CPM_PREF_MAGIC) != sizeof(uint32_t)) {
    prefs.end();
    return false;
  }

  bool ok = true;
  for (uint8_t i = 0; i < CPM_DRIVE_COUNT && ok; ++i) {
    if (!entries[i].mounted) continue;
    char key[4];
    snprintf(key, sizeof(key), "d%c", 'A' + i);

    String value;
    value.reserve(strlen(entries[i].path) + 24);
    value += cpmProfileToken(entries[i].profile);
    value += '|';
    value += entries[i].readOnly ? "ro" : "rw";
    value += '|';
    value += entries[i].path;

    size_t written = prefs.putString(key, value);
    ok = written == value.length();
  }
  prefs.end();
  return ok;
}

inline bool cpmStoreSaveDesired()
{
  CpmMountEntry expected[CPM_DRIVE_COUNT];
  memcpy(expected, cpmDesiredMounts, sizeof(expected));

  if (!cpmStoreWritePrefs(expected)) {
    Serial.println("CP/M media: NVS mount-table write failed");
    return false;
  }

  CpmMountEntry check[CPM_DRIVE_COUNT];
  bool found = false;
  if (!cpmStoreReadPrefs(check, found) || !found || !cpmMountArrayEqual(expected, check)) {
    Serial.println("CP/M media: NVS mount-table verification failed");
    return false;
  }
  return true;
}

inline bool cpmStoreFetchDiskDirectory(JsonDocument &reply, JsonArray &entries)
{
  JsonDocument request;
  request["files"]["path"] = CPM_DISK_ROOT;
  if (!ebtksStorageExchange(request, reply, 8000)) return false;

  // Current Teensy firmware returns
  // {"theFiles":{"path":"/cpm/disks"},"dir":[...]}. Prefer that layout,
  // while accepting the earlier nested form for compatibility with test builds.
  entries = reply["dir"].as<JsonArray>();
  if (!entries.isNull()) return true;
  entries = reply["theFiles"]["dir"].as<JsonArray>();
  return !entries.isNull();
}

inline bool cpmStoreFindMedia(const char *path, CpmMountEntry &entry, uint32_t *sizeOut = nullptr)
{
  cpmClearMount(entry);
  if (!cpmPathAllowed(path)) return false;

  const char *base = strrchr(path, '/');
  base = base ? base + 1 : path;

  JsonDocument listing;
  JsonArray files;
  if (!cpmStoreFetchDiskDirectory(listing, files)) return false;

  for (JsonObject item : files) {
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

inline bool cpmStoreBuildLegacyDefaults(CpmMountEntry *out)
{
  if (!out) return false;
  for (uint8_t i = 0; i < CPM_DRIVE_COUNT; ++i) cpmClearMount(out[i]);

  JsonDocument listing;
  JsonArray files;
  if (!cpmStoreFetchDiskDirectory(listing, files)) return false;

  for (uint8_t drive = 0; drive < CPM_DRIVE_COUNT; ++drive) {
    char legacyName[8];
    snprintf(legacyName, sizeof(legacyName), "%c.dsk", 'A' + drive);

    for (JsonObject item : files) {
      const char *type = item["type"] | "";
      const char *name = item["name"] | "";
      if (strcmp(type, "file") || strcasecmp(name, legacyName)) continue;

      char path[CPM_MEDIA_PATH_MAX];
      int n = snprintf(path, sizeof(path), "%s/%s", CPM_DISK_ROOT, name);
      if (n <= 0 || n >= (int)sizeof(path)) break;
      uint32_t size = item["size"] | 0UL;
      bool ro = item["readonly"] | false;
      cpmClassifyMedia(path, size, ro, out[drive]);
      break;
    }
  }
  return true;
}

inline bool cpmStoreLoadDesired()
{
  CpmMountEntry loaded[CPM_DRIVE_COUNT];
  bool found = false;
  if (!cpmStoreReadPrefs(loaded, found)) return false;

  if (!found) {
    // First try the SD-card table produced by the first media-manager build.
    // This preserves any assignments the user already selected before this
    // NVS-backed fix. If no usable table exists, import A.dsk/B.dsk/etc.
    if (cpmLoadDesiredMounts(false)) {
      memcpy(loaded, cpmDesiredMounts, sizeof(loaded));
      found = true;
      Serial.println("CP/M media: migrating SD mount table to ESP32 NVS");
    } else {
      if (!cpmStoreBuildLegacyDefaults(loaded)) return false;
      found = true;
      Serial.println("CP/M media: importing legacy drive filenames into ESP32 NVS");
    }

    if (!cpmStoreWritePrefs(loaded)) {
      Serial.println("CP/M media: unable to persist initial NVS mount table");
      return false;
    }
  }

  memcpy(cpmDesiredMounts, loaded, sizeof(cpmDesiredMounts));
  cpmDesiredLoaded = true;
  cpmStoreLoaded = true;
  return true;
}

inline bool cpmStoreMountDesired(uint8_t drive, const char *path)
{
  if (!cpmValidDrive(drive)) return false;
  if (!cpmStoreLoaded && !cpmStoreLoadDesired()) return false;

  CpmMountEntry media;
  if (!cpmStoreFindMedia(path, media)) return false;

  CpmMountEntry old = cpmDesiredMounts[drive];
  cpmDesiredMounts[drive] = media;
  if (!cpmStoreSaveDesired()) {
    cpmDesiredMounts[drive] = old;
    return false;
  }
  return true;
}

inline bool cpmStoreUnmountDesired(uint8_t drive)
{
  if (!cpmValidDrive(drive)) return false;
  if (!cpmStoreLoaded && !cpmStoreLoadDesired()) return false;

  CpmMountEntry old = cpmDesiredMounts[drive];
  cpmClearMount(cpmDesiredMounts[drive]);
  if (!cpmStoreSaveDesired()) {
    cpmDesiredMounts[drive] = old;
    return false;
  }
  return true;
}

inline bool cpmStoreVerifyDesired()
{
  if (!cpmStoreLoaded && !cpmStoreLoadDesired()) return false;
  CpmMountEntry check[CPM_DRIVE_COUNT];
  bool found = false;
  return cpmStoreReadPrefs(check, found) && found &&
         cpmMountArrayEqual(cpmDesiredMounts, check);
}

inline bool cpmStoreActivateMounts()
{
  cpmStoreLoaded = false;
  if (!cpmStoreLoadDesired()) {
    Serial.println("CP/M media: unable to load persistent mount table; no virtual media activated");
    for (auto &entry : cpmActiveMounts) cpmClearMount(entry);
    return false;
  }

  memcpy(cpmActiveMounts, cpmDesiredMounts, sizeof(cpmActiveMounts));

  // Validate each selected file against the current SD-card directory before
  // exposing it to CP/M. Missing media is left configured for the next retry,
  // but is unmounted for this boot.
  for (uint8_t drive = 0; drive < CPM_DRIVE_COUNT; ++drive) {
    if (!cpmActiveMounts[drive].mounted) continue;
    CpmMountEntry actual;
    if (!cpmStoreFindMedia(cpmActiveMounts[drive].path, actual) ||
        actual.profile != cpmActiveMounts[drive].profile) {
      Serial.printf("CP/M media: %c: configured image unavailable/changed: %s\n",
                    'A' + drive, cpmActiveMounts[drive].path);
      cpmClearMount(cpmActiveMounts[drive]);
    }
  }

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

inline bool cpmStoreBuildMediaStatusJson(String &out)
{
  if (!cpmStoreLoaded && !cpmStoreLoadDesired()) return false;

  JsonDocument result;
  result["ok"] = true;
  result["store"] = "esp32-nvs";
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
  JsonArray files;
  bool listingOk = cpmStoreFetchDiskDirectory(listing, files);
  result["media_ok"] = listingOk;
  JsonArray media = result["media"].to<JsonArray>();

  if (listingOk) {
    for (JsonObject item : files) {
      const char *type = item["type"] | "";
      const char *name = item["name"] | "";
      if (strcmp(type, "file") || !name[0]) continue;

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
