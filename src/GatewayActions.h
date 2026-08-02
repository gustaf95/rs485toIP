#pragma once

#include <Arduino.h>
#include "RoutingTable.h"
#include "Storage.h"
#include "Rs485Port.h"
#include "StatusLed.h"

// SerialMenu와 WebConfigServer가 공유하는 작은 액션들. 각 UI는 자기 화면에 맞는
// 확인 절차(타이핑 "YES", 웹 폼 버튼 등)만 책임지고, 실제 동작은 여기 한 곳에서만
// 구현해 두 UI가 서로 다르게 동작하는 걸 막는다.

// 설정을 config.h 기본값으로 되돌리고 flash에 저장, RS485 UART와 상태 LED 핀을 새
// 기본값으로 재적용한 뒤 재부팅한다. 정상적으로 반환하지 않는다 (ESP.restart()).
void performFactoryReset(RoutingTable& routing, Storage& storage, Rs485Port& rs485,
                          StatusLed& statusLed);

// Query Pan Position 테스트 패킷(주소 1 고정)을 만든다 - Pelco-D/P 둘 다 응답을
// 정의하고 있는 조회 명령이라 "뭔가 응답이 오는지" 테스트하기에 적합하다
// (doc/pelcoD_command.md 5절, doc/pelcoP_command.md 5절의 0x51/Response 0x59).
void buildPelcoDTestCommand(uint8_t out[7]);
void buildPelcoPTestCommand(uint8_t out[8]);

// cfg.wifi.apSsid/apPassword를 WiFi.softAP()에 그대로 재적용한다. Serial/Web 어느
// 쪽에서 AP SSID/Password를 바꾸든 재부팅 없이 즉시 반영되도록 공유한다.
void applyApSettings(const SystemConfig& cfg);

// RS485 UART0 Shared Mode를 켜거나 끈다. 켤 때는 RX/TX/DE-RE를 보드 고정 배선값
// (config.h RS485_UART0_SHARED_*)으로 강제하고 Debug Mode를 끈다 - Serial이 곧 RS485라
// 디버그 프린트가 writePacket()의 DE HIGH 구간과 겹쳐 나갈 위험을 아예 없애기 위함이다.
// 끌 때는 UART2 기본값(config.h RS485_*_DEFAULT)으로 되돌린다. 어느 쪽이든 재부팅해야
// HardwareSerial(특히 USB 콘솔)이 깨끗하게 다시 초기화되므로 정상적으로 반환하지
// 않는다 (ESP.restart()). 호출 전에 사용자에게 보여줄 안내 메시지는 호출자가 먼저
// 출력해야 한다 - 이 함수가 반환하지 않기 때문이다.
void setRs485Uart0SharedMode(RoutingTable& routing, Storage& storage, Rs485Port& rs485,
                              bool enabled);
