#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include "config.h"

enum class ProtocolMode : uint8_t {
  IP_VISCA_RAW_UDP = 0,
  SONY_VISCA_IP = 1
};

enum class OutputAddressMode : uint8_t {
  PRESERVE = 0,
  REWRITE_TO_0x81 = 1
};

enum class ResponseMode : uint8_t {
  NONE = 0,
  SYNTHETIC = 1,
  FORWARD = 2
};

enum class BroadcastMode : uint8_t {
  IGNORE = 0,
  FORWARD_TO_ALL = 1
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

struct TargetDevice {
  char name[16];
  bool enabled;
  uint8_t rs485ViscaId;
  uint8_t inputAddressByte;
  OutputAddressMode outputAddressMode;
  StoredIp cameraIp;
  uint16_t cameraPort;
  ProtocolMode protocolMode;
  ResponseMode responseMode;
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

  BroadcastMode broadcastMode;

  TargetDevice devices[MAX_TARGET_DEVICES];
  uint8_t deviceCount;
};

class DeviceConfig {
 public:
  void begin();
  void load();
  void save();
  void applyDefaults();

  SystemConfig& get() { return _config; }
  const SystemConfig& get() const { return _config; }

  TargetDevice* findByInputAddress(uint8_t addressByte);
  int addOrUpdateDevice(const TargetDevice& device);
  bool deleteDevice(uint8_t rs485ViscaId);

 private:
  SystemConfig _config;
};
