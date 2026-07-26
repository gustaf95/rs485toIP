#pragma once

#include "RoutingTable.h"

// SystemConfig를 ESP32 NVS(Preferences)에 저장/로드한다.
class Storage {
 public:
  // 저장된 설정을 config에 채워 넣는다. 저장된 설정이 없거나 magic/version이
  // 다르면 config를 건드리지 않고 false를 반환한다 (호출자가 기본값을 적용해야 함).
  bool load(SystemConfig& config);
  void save(const SystemConfig& config);
};
