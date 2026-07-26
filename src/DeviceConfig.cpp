#include "DeviceConfig.h"
#include <Preferences.h>
#include <string.h>

namespace {
const char* kNamespace = "visca-gw";
const char* kBlobKey = "config";
constexpr uint32_t kConfigMagic = 0x56494735;  // "VIG5"
constexpr uint16_t kConfigVersion = 1;
}  // namespace

void DeviceConfig::applyDefaults() {
  memset(&_config, 0, sizeof(_config));

  _config.magic = kConfigMagic;
  _config.version = kConfigVersion;

  _config.wifi.ssid[0] = '\0';
  _config.wifi.password[0] = '\0';
  _config.wifi.useDhcp = true;

  _config.rs485Baudrate = RS485_BAUD_DEFAULT;
  _config.rs485RxPin = RS485_RX_PIN_DEFAULT;
  _config.rs485TxPin = RS485_TX_PIN_DEFAULT;
  _config.rs485DeRePin = RS485_DE_RE_PIN_DEFAULT;

  _config.broadcastMode = BroadcastMode::IGNORE;

  TargetDevice cam5 = {};
  strncpy(cam5.name, "CAM5", sizeof(cam5.name) - 1);
  cam5.enabled = true;
  cam5.rs485ViscaId = 5;
  cam5.inputAddressByte = VISCA_ADDR_CAM5;
  cam5.outputAddressMode = OutputAddressMode::REWRITE_TO_0x81;
  cam5.cameraIp = {{0, 0, 0, 0}};  // 웹 설정에서 입력 필요
  cam5.cameraPort = DEFAULT_CAMERA_PORT;
  cam5.protocolMode = ProtocolMode::IP_VISCA_RAW_UDP;
  cam5.responseMode = ResponseMode::SYNTHETIC;

  _config.devices[0] = cam5;
  _config.deviceCount = 1;
}

void DeviceConfig::begin() {
  load();
}

void DeviceConfig::load() {
  Preferences prefs;
  prefs.begin(kNamespace, /*readOnly=*/true);

  SystemConfig loaded;
  size_t got = prefs.getBytes(kBlobKey, &loaded, sizeof(loaded));
  prefs.end();

  if (got == sizeof(loaded) && loaded.magic == kConfigMagic &&
      loaded.version == kConfigVersion) {
    _config = loaded;
  } else {
    // 저장된 설정이 없거나 형식이 다르면 CAM5 기본 구조로 시작한다.
    applyDefaults();
    save();
  }
}

void DeviceConfig::save() {
  Preferences prefs;
  prefs.begin(kNamespace, /*readOnly=*/false);
  prefs.putBytes(kBlobKey, &_config, sizeof(_config));
  prefs.end();
}

TargetDevice* DeviceConfig::findByInputAddress(uint8_t addressByte) {
  for (uint8_t i = 0; i < _config.deviceCount; i++) {
    if (_config.devices[i].inputAddressByte == addressByte) {
      return &_config.devices[i];
    }
  }
  return nullptr;
}

int DeviceConfig::addOrUpdateDevice(const TargetDevice& device) {
  for (uint8_t i = 0; i < _config.deviceCount; i++) {
    if (_config.devices[i].rs485ViscaId == device.rs485ViscaId) {
      _config.devices[i] = device;
      return i;
    }
  }

  if (_config.deviceCount >= MAX_TARGET_DEVICES) {
    return -1;
  }

  _config.devices[_config.deviceCount] = device;
  return _config.deviceCount++;
}

bool DeviceConfig::deleteDevice(uint8_t rs485ViscaId) {
  for (uint8_t i = 0; i < _config.deviceCount; i++) {
    if (_config.devices[i].rs485ViscaId == rs485ViscaId) {
      for (uint8_t j = i; j < _config.deviceCount - 1; j++) {
        _config.devices[j] = _config.devices[j + 1];
      }
      _config.deviceCount--;
      return true;
    }
  }
  return false;
}
