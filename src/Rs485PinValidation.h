#pragma once

#include <Arduino.h>

// ESP32 GPIO 제약사항에 따라 RS485 RX/TX/DE-RE 핀으로 쓸 수 있는지 검사한다.
// SerialMenu와 WebConfigServer가 동일한 규칙을 쓰도록 공유한다 - 둘 중 하나만
// 검증 규칙을 갱신하면 두 UI가 서로 다른 핀을 허용/거부하게 되어버린다.

// GPIO1/3: UART0(USB Serial 메뉴 전용), GPIO6~11: 내장 SPI Flash 전용 - 절대 사용 불가.
bool isReservedGpio(uint8_t pin);

// GPIO34~39: 입력 전용 - 출력(TX, DE/RE)으로는 사용 불가.
bool isInputOnlyGpio(uint8_t pin);

// 부팅 모드를 결정하는 스트래핑 핀 - 사용은 가능하나 외부 배선에 따라 부팅에
// 영향을 줄 수 있어 경고만 표시한다 (validateRs485Pin은 이 핀을 거부하지 않음).
bool isStrappingGpio(uint8_t pin);

// pin이 RS485 RX/TX/DE-RE로 쓰기에 유효하면 true를 반환한다. 유효하지 않으면
// false를 반환하고 *errorOut에 이유를 채운다. requireOutput은 TX/DE-RE처럼
// 출력이 필요한 핀인지 여부(RX 검사 시에는 false로 호출).
bool validateRs485Pin(int pin, bool requireOutput, String* errorOut);
