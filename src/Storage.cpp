#include "Storage.h"

#include <Preferences.h>
#include <stddef.h>

namespace {
const char* kNamespace = "visca-gw";
const char* kBlobKey = "config";
constexpr uint32_t kConfigMagic = 0x56494738;  // "VIG8" (bumped for the camera-slot layout)

// 현재 레이아웃 버전. **필드를 구조체 끝에 덧붙일 때만** 올린다.
constexpr uint16_t kConfigVersion = 11;  // bumped: SystemConfig 끝에 statusLedActiveLow 추가

// 여기까지의 버전은 현재 구조체의 **접두사**라는 것이 보장된다 - 즉 v10 블롭을 그대로
// 앞에서부터 읽어도 필드가 밀리지 않는다. v9 이하는 CameraSlot 중간에 필드가 들어간
// 적이 있어(autoPowerControl) 접두사가 아니므로 받아주면 안 된다.
constexpr uint16_t kMinCompatVersion = 10;

// v10 블롭의 크기. 새 필드가 statusLedActiveLow부터 시작하므로 그 오프셋이 곧 옛 크기다.
constexpr size_t kV10Size = offsetof(SystemConfig, statusLedActiveLow);

// 새 필드를 cameras[] 뒤가 아니라 중간에 끼워 넣으면 위 "접두사" 전제가 깨진다.
// 그러면 옛 펌웨어가 저장한 바이트가 통째로 한 칸씩 밀려 엉뚱한 값으로 읽히는데,
// 그건 설정이 초기화되는 것보다 훨씬 나쁘다(잘못된 핀으로 조용히 동작한다).
static_assert(offsetof(SystemConfig, statusLedActiveLow) >=
                  offsetof(SystemConfig, cameras) + sizeof(SystemConfig::cameras),
              "new SystemConfig fields must be appended after cameras[]");
}  // namespace

// 저장된 블롭이 지금 구조체보다 짧아도(옛 버전) 받아준다.
//
// 예전에는 `got == sizeof(loaded) && version == kConfigVersion`을 요구해서, 설정을
// 하나만 추가해도 현장 장비의 Wi-Fi/라우팅/핀 설정이 전부 날아갔다(readme.md 4.2절).
// 그래서 "안 쓰는 필드라도 구조체에서 빼지 말 것"이라는 제약까지 생겼다.
//
// 지금은 호출자가 넣어둔 기본값 위에 저장된 바이트를 덮어쓰는 방식이라, 옛 블롭에
// 없던 뒷부분은 자연히 기본값으로 남는다. Preferences::getBytes()는 저장된 길이가
// 버퍼보다 **크면** 아무것도 쓰지 않고 0을 반환하므로, 새 펌웨어가 쓴 블롭을 옛
// 펌웨어가 읽는 경우(다운그레이드)도 조용히 깨지지 않고 기본값으로 안전하게 떨어진다.
bool Storage::load(SystemConfig& config, bool* upgradedOut) {
  Preferences prefs;
  prefs.begin(kNamespace, /*readOnly=*/true);

  // **기본값 위에 덮어쓴다.** 호출자는 load() 전에 applyDefaults()를 부른 상태다
  // (main.cpp setup() 참고) - 그 값이 옛 블롭에 없는 뒷부분을 메운다.
  SystemConfig loaded = config;
  size_t got = prefs.getBytes(kBlobKey, &loaded, sizeof(loaded));
  prefs.end();

  if (got < kV10Size) return false;  // 저장된 것이 없거나, 해석할 수 없을 만큼 짧다
  if (loaded.magic != kConfigMagic) return false;
  if (loaded.version < kMinCompatVersion || loaded.version > kConfigVersion) return false;

  if (upgradedOut) *upgradedOut = (loaded.version < kConfigVersion);

  config = loaded;
  return true;
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
