#include "Rs485Port.h"

void Rs485Port::begin(uint32_t baudrate, uint8_t rxPin, uint8_t txPin, uint8_t deRePin) {
  _deRePin = deRePin;

  pinMode(_deRePin, OUTPUT);
  digitalWrite(_deRePin, LOW);  // 기본 수신 모드

  // RS485가 USB 콘솔과 같은 UART0(Serial)를 공유하므로, 여기서 시작한 baudrate가
  // 곧 USB 시리얼 터미널에서 메뉴를 보기 위해 맞춰야 하는 baudrate이기도 하다.
  Serial.begin(baudrate, SERIAL_8N1, rxPin, txPin);
}

bool Rs485Port::available() {
  return Serial.available() > 0;
}

uint8_t Rs485Port::read() {
  return Serial.read();
}

void Rs485Port::writePacket(const uint8_t* data, uint8_t len) {
  digitalWrite(_deRePin, HIGH);  // 송신 모드
  delayMicroseconds(50);         // 트랜시버 전환 시간 확보

  Serial.write(data, len);
  Serial.flush();  // 마지막 바이트까지 실제로 출력될 때까지 대기

  digitalWrite(_deRePin, LOW);  // 즉시 수신 모드로 복귀
}
