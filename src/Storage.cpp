#include "Storage.h"

#include <Preferences.h>
#include <stddef.h>

namespace {
const char* kNamespace = "visca-gw";
const char* kBlobKey = "config";
constexpr uint32_t kConfigMagic = 0x56494738;  // "VIG8" (bumped for the camera-slot layout)

// 현재 레이아웃 버전. **필드를 구조체 끝에 덧붙일 때만** 올린다.
//
// wifiBootDelaySec는 구조체 끝에 붙었지만 버전을 올리지 않았다 - 블롭이 아니라 별도 키에
// 저장하기 때문이다(아래 kBlobSize, kBootDelayKey 참고). 블롭은 v13과 바이트 단위로 같다.
constexpr uint16_t kConfigVersion = 13;

// 잠깐 배포됐던 빌드(d5775c0)가 쓴 블롭. 레이아웃은 v13 + 끝의 wifiBootDelaySec 그대로라
// 받아준다 - 안 받으면 그 빌드로 설정을 저장한 장비가 이 펌웨어에서 전부 초기화된다.
constexpr uint16_t kLegacyBootDelayBlobVersion = 14;

// 블롭에 쓰는 크기. **wifiBootDelaySec 앞까지만** 쓴다.
//
// 그 필드까지 블롭에 넣으면 버전을 올려야 하고, 그러면 옛 펌웨어로 되돌려 올린 장비가
// `version > kConfigVersion`(또는 블롭이 버퍼보다 커서 getBytes()가 0)으로 load()에
// 실패해 Wi-Fi/터널/라우팅 설정이 전부 기본값이 된다. 새 설정을 블롭 밖 키에 두면
// 업그레이드든 다운그레이드든 블롭은 옛 펌웨어가 그대로 읽을 수 있는 모양으로 남는다.
// 옛 펌웨어는 모르는 키를 그냥 무시한다.
//
// 앞으로 설정을 추가할 때도 같은 방식(별도 키)을 쓰면 버전을 올릴 일이 없다.
constexpr size_t kBlobSize = offsetof(SystemConfig, wifiBootDelaySec);
const char* kBootDelayKey = "wifiBootDly";  // NVS 키는 15자 이하

// 여기까지의 버전은 현재 구조체의 **접두사**라는 것이 보장된다 - 즉 v10 블롭을 그대로
// 앞에서부터 읽어도 필드가 밀리지 않는다. v9 이하는 CameraSlot 중간에 필드가 들어간
// 적이 있어(autoPowerControl) 접두사가 아니므로 받아주면 안 된다.
constexpr uint16_t kMinCompatVersion = 10;

// v10 블롭의 크기. 새 필드가 statusLedActiveLow부터 시작하므로 그 오프셋이 곧 옛 크기다.
constexpr size_t kV10Size = offsetof(SystemConfig, statusLedActiveLow);

// 새 필드를 cameras[] 뒤가 아니라 중간에 끼워 넣으면 위 "접두사" 전제가 깨진다.
// 그러면 옛 펌웨어가 저장한 바이트가 통째로 한 칸씩 밀려 엉뚱한 값으로 읽히는데,
// 그건 설정이 초기화되는 것보다 훨씬 나쁘다(잘못된 핀으로 조용히 동작한다).
static_assert(offsetof(SystemConfig, wifiBootDelaySec) >=
                  offsetof(SystemConfig, tunnelNoiseFilter) + sizeof(SystemConfig::tunnelNoiseFilter),
              "wifiBootDelaySec must be appended after tunnelNoiseFilter");
static_assert(offsetof(SystemConfig, tunnelNoiseFilter) >=
                  offsetof(SystemConfig, tunnelPeer) + sizeof(SystemConfig::tunnelPeer),
              "tunnelNoiseFilter must be appended after tunnelPeer");
static_assert(offsetof(SystemConfig, tunnelRole) >=
                  offsetof(SystemConfig, statusLedActiveLow) + sizeof(SystemConfig::statusLedActiveLow),
              "tunnel fields must be appended after statusLedActiveLow");
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
  bool hasBootDelayKey = prefs.isKey(kBootDelayKey);
  uint16_t bootDelay = prefs.getUShort(kBootDelayKey, config.wifiBootDelaySec);
  prefs.end();

  if (got < kV10Size) return false;  // 저장된 것이 없거나, 해석할 수 없을 만큼 짧다
  if (loaded.magic != kConfigMagic) return false;
  bool legacyBootDelayBlob = (loaded.version == kLegacyBootDelayBlobVersion);
  if (!legacyBootDelayBlob &&
      (loaded.version < kMinCompatVersion || loaded.version > kConfigVersion)) {
    return false;
  }

  if (upgradedOut) *upgradedOut = (loaded.version < kConfigVersion);

  // **블롭 안의 wifiBootDelaySec 자리는 믿지 않는다.** v13 블롭에서 그 자리는 구조체 끝
  // 패딩이라 아무 바이트나 들어 있을 수 있다 - 그대로 읽으면 운이 나쁠 때 몇 시간짜리
  // 지연이 된다. 값은 별도 키에서 가져오고, 키가 없으면 기본값이다. 예외는 d5775c0 빌드의
  // 블롭뿐이다 - 그 빌드는 이 값을 블롭 안에 진짜로 저장했다.
  if (!hasBootDelayKey && legacyBootDelayBlob) bootDelay = loaded.wifiBootDelaySec;
  loaded.wifiBootDelaySec = (bootDelay <= WIFI_BOOT_DELAY_S_MAX) ? bootDelay : config.wifiBootDelaySec;

  config = loaded;
  return true;
}

void Storage::save(const SystemConfig& config) {
  SystemConfig toSave = config;
  toSave.magic = kConfigMagic;
  toSave.version = kConfigVersion;

  Preferences prefs;
  prefs.begin(kNamespace, /*readOnly=*/false);
  prefs.putBytes(kBlobKey, &toSave, kBlobSize);
  prefs.putUShort(kBootDelayKey, config.wifiBootDelaySec);
  prefs.end();
}
