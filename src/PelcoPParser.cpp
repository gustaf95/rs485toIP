#include "PelcoPParser.h"

PelcoPParseResult PelcoPParser::feed(uint8_t b) {
  // 프레임이 시작되기 전이면 시작 바이트(0xA0)가 나올 때까지 무시한다 -
  // 노이즈나 프레임 중간 진입 시 다음 0xA0에서 재동기화하기 위함이다.
  if (_length == 0 && b != PELCO_P_START_BYTE) {
    return PelcoPParseResult::NONE;
  }

  _buffer[_length++] = b;
  _lastByteMillis = millis();

  if (_length < PELCO_P_PACKET_LEN) {
    return PelcoPParseResult::NONE;
  }

  // 8바이트 모두 채워짐 - ETX(Byte7)가 고정값 0xAF인지, Address~Data2
  // (Byte2~Byte6) XOR이 Checksum(Byte8)과 일치하는지 검증한다.
  if (_buffer[6] != PELCO_P_ETX_BYTE) {
    reset();
    return PelcoPParseResult::CHECKSUM_ERROR;
  }

  uint8_t checksum = 0;
  for (uint8_t i = 1; i < 6; i++) {
    checksum ^= _buffer[i];
  }

  if (checksum == _buffer[PELCO_P_PACKET_LEN - 1]) {
    return PelcoPParseResult::PACKET_READY;
  }
  reset();
  return PelcoPParseResult::CHECKSUM_ERROR;
}

PelcoPParseResult PelcoPParser::poll() {
  if (_length > 0 && (millis() - _lastByteMillis) > PELCO_P_PACKET_TIMEOUT_MS) {
    reset();
    return PelcoPParseResult::TIMEOUT_DISCARD;
  }
  return PelcoPParseResult::NONE;
}

void PelcoPParser::reset() {
  _length = 0;
}
