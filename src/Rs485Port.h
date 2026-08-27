#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include "config.h"

// RS485 송수신 및 DE/RE 방향 제어를 담당한다. USB Serial 콘솔과 겹치지 않는 독립
// UART를 쓴다 - 어느 번호인지는 보드마다 다르므로 BoardProfile.h가 정한다
// (클래식은 UART2, C3는 UART가 두 개뿐이라 UART1).
class Rs485Port {
 public:
  // invert=true면 ESP32 UART 하드웨어가 RX/TX 신호를 모두 반전시킨다 - A/B(D+/D-)가
  // 뒤집혀 결선된 RS485 버스를 소프트웨어로 보정한다.
  void begin(uint32_t baudrate, uint8_t rxPin, uint8_t txPin, uint8_t deRePin, bool invert);

  bool available();
  uint8_t read();

  // 송신 시에만 DE/RE를 HIGH로 올리고, 송신 완료 후 즉시 수신 모드로 되돌린다.
  void writePacket(const uint8_t* data, uint8_t len);

  // 송신한 패킷을 그대로 넘겨받는 콜백. 게이트웨이는 자기가 보낸 바이트를 자기 RX로
  // 되들을 수 없어서(송신 중에는 DE/RE가 수신부를 끈다) Raw Byte Monitor 같은 관찰
  // 도구에서 송신이 통째로 보이지 않는데, 이 훅으로 그 공백을 메운다. 실제 송신이 끝난
  // 뒤에 호출되므로 콜백이 느려도 버스 타이밍에는 영향을 주지 않는다.
  using TxEchoFn = void (*)(const uint8_t* data, uint8_t len);
  void setTxEcho(TxEchoFn fn) { _txEcho = fn; }

 private:
  // **번호를 여기 직접 적지 않는다.** C3처럼 UART가 두 개뿐인 칩에 2를 넘기면
  // HardwareSerial::begin()이 조용히 return해버려서(BoardProfile.h 주석 참고)
  // 컴파일도 부팅도 되는데 RS485만 죽는다.
  HardwareSerial _uart{BOARD_RS485_UART_NUM};
  HardwareSerial* _serial = &_uart;
  uint8_t _deRePin = RS485_DE_RE_PIN_DEFAULT;
  TxEchoFn _txEcho = nullptr;
};
