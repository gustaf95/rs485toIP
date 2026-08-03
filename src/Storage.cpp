#include "Storage.h"
#include <Preferences.h>

namespace {
const char* kNamespace = "visca-gw";
const char* kBlobKey = "config";
constexpr uint32_t kConfigMagic = 0x56494738;  // "VIG8" (bumped for the camera-slot layout)
constexpr uint16_t kConfigVersion = 10;  // bumped: CameraSlot에 autoPowerControl 필드 추가
}  // namespace

bool Storage::load(SystemConfig& config) {
  Preferences prefs;
  prefs.begin(kNamespace, /*readOnly=*/true);

  SystemConfig loaded;
  size_t got = prefs.getBytes(kBlobKey, &loaded, sizeof(loaded));
  prefs.end();

  if (got == sizeof(loaded) && loaded.magic == kConfigMagic && loaded.version == kConfigVersion) {
    config = loaded;
    return true;
  }
  return false;
}

void Storage::save(const SystemConfig& config) {
  SystemConfig toSave = config;
  toSave.magic = kConfigMagic;
  toSave.version = kConfigVersion;

  Preferences prefs;
  prefs.begin(kNamespace, /*readOnly=*/false);
  prefs.putBytes(kBlobKey, &toSave, sizeof(toSave));
  prefs.end();
}
