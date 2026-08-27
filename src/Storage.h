#pragma once

#include "RoutingTable.h"

// SystemConfig를 ESP32 NVS(Preferences)에 저장/로드한다.
class Storage {
 public:
  // 저장된 설정을 config에 채워 넣는다. config는 호출 전에 applyDefaults()로 채워져
  // 있어야 한다 - 옛 펌웨어가 저장한 짧은 블롭을 읽을 때 뒤쪽 새 필드를 그 기본값으로
  // 남기는 방식이라, 기본값이 없으면 쓰레기가 들어간다 (Storage.cpp 주석 참고).
  //
  // 저장된 설정이 없거나 magic/버전이 해석 불가능하면 config를 건드리지 않고 false를
  // 반환한다 (호출자가 기본값 그대로 쓰면 된다).
  //
  // upgradedOut이 주어지면, 옛 버전 블롭을 읽어 새 필드를 기본값으로 메운 경우에만
  // true가 담긴다 - 호출자가 그때 한 번 다시 저장해 flash를 최신 레이아웃으로
  // 올려두라는 신호다. 매 부팅마다 flash에 쓰지 않기 위해 필요하다.
  bool load(SystemConfig& config, bool* upgradedOut = nullptr);

  void save(const SystemConfig& config);
};
