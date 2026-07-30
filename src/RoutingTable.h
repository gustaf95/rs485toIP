#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include "config.h"

enum class ProtocolMode : uint8_t {
  IP_VISCA_RAW_UDP = 0,
  IP_VISCA_RAW_TCP = 1,
  SONY_VISCA_UDP = 2
};

enum class AddressMode : uint8_t {
  REWRITE_0x81 = 0,
  PRESERVE = 1,
  REWRITE_BY_CAM = 2
};

enum class ResponseMode : uint8_t {
  NONE = 0,
  SYNTHETIC = 1,
  FORWARD = 2,
  FORWARD_REWRITE = 3
};

// IPv4 address stored as raw bytes so the struct stays a flat, blob-safe
// layout for Preferences storage (avoids depending on IPAddress internals).
struct StoredIp {
  uint8_t bytes[4];

  IPAddress toIPAddress() const {
    return IPAddress(bytes[0], bytes[1], bytes[2], bytes[3]);
  }

  void fromIPAddress(const IPAddress& ip) {
    bytes[0] = ip[0];
    bytes[1] = ip[1];
    bytes[2] = ip[2];
    bytes[3] = ip[3];
  }

  bool isZero() const {
    return bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0;
  }
};

// 카메라 1~7 고정 슬롯 하나의 설정. IP가 비어 있으면 해당 슬롯은 미사용으로 취급한다.
struct CameraSlot {
  StoredIp ip;
  uint16_t port;
  ProtocolMode protocol;
  AddressMode addressMode;

  bool isConfigured() const { return !ip.isZero(); }
};

struct WifiConfig {
  char ssid[32];
  char password[64];
  bool useDhcp;
  StoredIp staticIp;
  StoredIp gateway;
  StoredIp subnet;
};

struct SystemConfig {
  uint32_t magic;
  uint16_t version;

  WifiConfig wifi;

  uint32_t rs485Baudrate;
  uint8_t rs485RxPin;
  uint8_t rs485TxPin;
  uint8_t rs485DeRePin;

  ResponseMode responseMode;

  CameraSlot cameras[CAMERA_SLOT_COUNT];  // index 0 = CAM1 ... index 6 = CAM7
};

struct RoutedPacket {
  CameraSlot* slot;
  uint8_t camNumber;  // 1~7
  uint8_t output[VISCA_BUFFER_SIZE];
  uint8_t outputLen;
};

// 카메라 1~7 고정 슬롯 설정을 보관하고, RS485에서 받은 VISCA 패킷의 첫 바이트를
// 보고 어느 카메라로 보낼지, 주소 바이트를 어떻게 재작성할지 결정한다.
class RoutingTable {
 public:
  void applyDefaults();

  SystemConfig& get() { return _config; }
  const SystemConfig& get() const { return _config; }

  // camNumber는 1~7. 범위를 벗어나면 nullptr.
  CameraSlot* camera(uint8_t camNumber);

  // 매칭된 대상 수를 반환한다 (0이면 무시됨: 카메라 번호에 IP 미설정, 또는
  // broadcast인데 IP가 설정된 카메라가 하나도 없는 경우).
  uint8_t route(const uint8_t* input, uint8_t inputLen, RoutedPacket* results, uint8_t maxResults);

 private:
  SystemConfig _config;

  void buildOutputPacket(const CameraSlot& slot, uint8_t camNumber, const uint8_t* input,
                         uint8_t inputLen, RoutedPacket* result);
};
