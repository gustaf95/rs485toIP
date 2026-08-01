#pragma once

#include <Arduino.h>
#include "RoutingTable.h"
#include "Storage.h"
#include "Rs485Port.h"

// SerialMenu와 WebConfigServer가 공유하는 작은 액션들. 각 UI는 자기 화면에 맞는
// 확인 절차(타이핑 "YES", 웹 폼 버튼 등)만 책임지고, 실제 동작은 여기 한 곳에서만
// 구현해 두 UI가 서로 다르게 동작하는 걸 막는다.

// 설정을 config.h 기본값으로 되돌리고 flash에 저장, RS485 UART를 새 기본값으로
// 재적용한 뒤 재부팅한다. 정상적으로 반환하지 않는다 (ESP.restart()).
void performFactoryReset(RoutingTable& routing, Storage& storage, Rs485Port& rs485);

// Query Pan Position 테스트 패킷(주소 1 고정)을 만든다 - Pelco-D/P 둘 다 응답을
// 정의하고 있는 조회 명령이라 "뭔가 응답이 오는지" 테스트하기에 적합하다
// (doc/pelcoD_command.md 5절, doc/pelcoP_command.md 5절의 0x51/Response 0x59).
void buildPelcoDTestCommand(uint8_t out[7]);
void buildPelcoPTestCommand(uint8_t out[8]);

// cfg.wifi.apSsid/apPassword를 WiFi.softAP()에 그대로 재적용한다. Serial/Web 어느
// 쪽에서 AP SSID/Password를 바꾸든 재부팅 없이 즉시 반영되도록 공유한다.
void applyApSettings(const SystemConfig& cfg);
