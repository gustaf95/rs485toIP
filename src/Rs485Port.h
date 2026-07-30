#pragma once

#include <Arduino.h>
#include "config.h"

// 이 하드웨어 리비전은 RS485가 RX0/TX0(UART0)에 고정 결선되어 있어, USB 시리얼
// 콘솔(메뉴/디버그)과 물리적으로 같은 UART(Serial)를 공유한다. 별도의 UART2
// 포트를 쓰지 않으며, 실제로 콘솔에 타이핑하는 동안 RS485 트래픽이 들어오면
// 서로의 파싱 로직에 섞여 보일 수 있다.
class Rs485Port {
 public:
  void begin(uint32_t baudrate, uint8_t rxPin, uint8_t txPin, uint8_t deRePin);

  bool available();
  uint8_t read();

  // 송신 시에만 DE/RE를 HIGH로 올리고, 송신 완료 후 즉시 수신 모드로 되돌린다.
  void writePacket(const uint8_t* data, uint8_t len);

 private:
  uint8_t _deRePin = RS485_DE_RE_PIN_DEFAULT;
};
