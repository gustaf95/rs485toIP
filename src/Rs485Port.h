#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include "config.h"

// UART2 기반 RS485 송수신 및 DE/RE 방향 제어를 담당한다.
// TX0/RX0(UART0, USB Serial)는 절대 사용하지 않는다.
class Rs485Port {
 public:
  void begin(uint32_t baudrate, uint8_t rxPin, uint8_t txPin, uint8_t deRePin);

  bool available();
  uint8_t read();

  // 송신 시에만 DE/RE를 HIGH로 올리고, 송신 완료 후 즉시 수신 모드로 되돌린다.
  void writePacket(const uint8_t* data, uint8_t len);

 private:
  HardwareSerial _serial{2};
  uint8_t _deRePin = RS485_DE_RE_PIN_DEFAULT;
};
