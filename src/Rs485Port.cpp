#include "Rs485Port.h"

void Rs485Port::begin(uint32_t baudrate, uint8_t rxPin, uint8_t txPin, uint8_t deRePin,
                       bool invert) {
  _deRePin = deRePin;

  pinMode(_deRePin, OUTPUT);
  digitalWrite(_deRePin, LOW);  // 기본 수신 모드

  // 수신 링버퍼를 기본값(256)보다 크게 잡는다 - begin() 전에 호출해야 적용된다.
  // Debug Mode 로그 출력처럼 loop()를 잠시 붙잡는 작업이 있을 때, 그동안 들어온
  // RS485 바이트가 넘쳐 유실되면 프레임이 깨지고 Stop 명령까지 사라질 수 있다.
  _serial->setRxBufferSize(RS485_RX_BUFFER_SIZE);

  // begin()의 5번째 인자가 신호 반전 플래그다. ESP32 Arduino core는 이 플래그 하나로
  // RXD/TXD 두 신호를 함께 반전시키므로(uart_set_line_inverse에 UART_SIGNAL_RXD_INV |
  // UART_SIGNAL_TXD_INV를 같이 넘긴다), 수신뿐 아니라 합성 ACK 송신도 같은 극성으로
  // 나간다 - A/B가 뒤집힌 버스에서는 양방향 모두 이게 맞다.
  _serial->begin(baudrate, SERIAL_8N1, rxPin, txPin, invert);
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

  // 수신 모드로 돌아온 뒤에 echo한다 - 콜백이 Serial 출력처럼 느린 작업이어도
  // DE/RE를 붙잡고 있는 시간이 늘어나지 않는다.
  if (_txEcho) _txEcho(data, len);
}
