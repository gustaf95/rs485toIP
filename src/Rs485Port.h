#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include "config.h"

// RS485 송수신 및 DE/RE 방향 제어를 담당한다. 기본은 독립된 UART2를 쓰지만,
// RS485가 UART0(USB Serial)에 고정 결선된 보드를 위해 useUart0=true로 UART0를
// 공유하는 것도 지원한다 (config.h의 RS485_UART0_SHARED_* 참고).
class Rs485Port {
 public:
  void begin(uint32_t baudrate, uint8_t rxPin, uint8_t txPin, uint8_t deRePin, bool useUart0);

  bool available();
  uint8_t read();

  // 송신 시에만 DE/RE를 HIGH로 올리고, 송신 완료 후 즉시 수신 모드로 되돌린다.
  void writePacket(const uint8_t* data, uint8_t len);

 private:
  HardwareSerial _uart2{2};
  HardwareSerial* _serial = &_uart2;  // useUart0=true면 begin()에서 전역 Serial(UART0)로 바뀐다.
  uint8_t _deRePin = RS485_DE_RE_PIN_DEFAULT;
};
