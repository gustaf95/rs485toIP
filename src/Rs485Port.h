#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>
#include "config.h"

// RS485 송수신 및 DE/RE 방향 제어를 담당한다. 기본은 독립된 UART2를 쓰지만,
// RS485가 UART0(USB Serial)에 고정 결선된 보드를 위해 useUart0=true로 UART0를
// 공유하는 것도 지원한다 (config.h의 RS485_UART0_SHARED_* 참고).
class Rs485Port {
 public:
  // invert=true면 ESP32 UART 하드웨어가 RX/TX 신호를 모두 반전시킨다 - A/B(D+/D-)가
  // 뒤집혀 결선된 RS485 버스를 소프트웨어로 보정한다. UART0 공유 모드에서는 이 포트가
  // 곧 USB 콘솔이기도 하므로, 반전을 켜면 콘솔 쪽 신호도 같이 뒤집힌다는 점에 주의.
  void begin(uint32_t baudrate, uint8_t rxPin, uint8_t txPin, uint8_t deRePin, bool useUart0,
             bool invert);

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
  HardwareSerial _uart2{2};
  HardwareSerial* _serial = &_uart2;  // useUart0=true면 begin()에서 전역 Serial(UART0)로 바뀐다.
  uint8_t _deRePin = RS485_DE_RE_PIN_DEFAULT;
  TxEchoFn _txEcho = nullptr;
};
