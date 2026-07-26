#pragma once

#include <Arduino.h>
#include "DeviceConfig.h"
#include "config.h"

struct RoutedPacket {
  TargetDevice* device;
  uint8_t output[VISCA_BUFFER_SIZE];
  uint8_t outputLen;
};

// VISCA 패킷의 첫 바이트(주소 바이트)를 보고 Target Device를 선택하고,
// Output VISCA Address Mode 설정에 따라 주소 바이트를 재작성한다.
class DeviceRouter {
 public:
  explicit DeviceRouter(DeviceConfig& config) : _config(config) {}

  // 매칭된 대상 수를 반환한다 (0이면 무시됨: 등록되지 않은 주소 또는
  // broadcast가 ignore로 설정된 경우).
  uint8_t route(const uint8_t* input, uint8_t inputLen, RoutedPacket* results, uint8_t maxResults);

 private:
  DeviceConfig& _config;

  void buildOutputPacket(const TargetDevice& device, const uint8_t* input,
                         uint8_t inputLen, RoutedPacket* result);
};
