#include "Rs485Port.h"

void Rs485Port::begin(uint32_t baudrate, uint8_t rxPin, uint8_t txPin, uint8_t deRePin,
                       bool useUart0) {
  _deRePin = deRePin;
  _serial = useUart0 ? &Serial : &_uart2;

  pinMode(_deRePin, OUTPUT);
  digitalWrite(_deRePin, LOW);  // 기본 수신 모드

  _serial->begin(baudrate, SERIAL_8N1, rxPin, txPin);
}

bool Rs485Port::available() {
  return _serial->available() > 0;
}

uint8_t Rs485Port::read() {
  return _serial->read();
}

void Rs485Port::writePacket(const uint8_t* data, uint8_t len) {
  digitalWrite(_deRePin, HIGH);  // 송신 모드
  delayMicroseconds(50);         // 트랜시버 전환 시간 확보

  _serial->write(data, len);
  _serial->flush();  // 마지막 바이트까지 실제로 출력될 때까지 대기

  digitalWrite(_deRePin, LOW);  // 즉시 수신 모드로 복귀
}
