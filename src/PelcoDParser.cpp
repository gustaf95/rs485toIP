#include "PelcoDParser.h"

PelcoDParseResult PelcoDParser::feed(uint8_t b) {
  // 프레임이 시작되기 전이면 시작 바이트(0xFF)가 나올 때까지 무시한다 -
  // 노이즈나 프레임 중간 진입 시 다음 0xFF에서 재동기화하기 위함이다.
  if (_length == 0 && b != PELCO_D_START_BYTE) {
    return PelcoDParseResult::NONE;
  }

  _buffer[_length++] = b;
  _lastByteMillis = millis();

  if (_length < PELCO_D_PACKET_LEN) {
    return PelcoDParseResult::NONE;
  }

  // 7바이트 모두 채워짐 - Address~Data2(Byte2~Byte6) 합산이 Checksum(Byte7)과
  // 일치하는지 검증한다.
  uint8_t sum = 0;
  for (uint8_t i = 1; i < PELCO_D_PACKET_LEN - 1; i++) {
    sum += _buffer[i];
  }

  if (sum == _buffer[PELCO_D_PACKET_LEN - 1]) {
    return PelcoDParseResult::PACKET_READY;
  }
  reset();
  return PelcoDParseResult::CHECKSUM_ERROR;
}

PelcoDParseResult PelcoDParser::poll() {
  if (_length > 0 && (millis() - _lastByteMillis) > PELCO_D_PACKET_TIMEOUT_MS) {
    reset();
    return PelcoDParseResult::TIMEOUT_DISCARD;
  }
  return PelcoDParseResult::NONE;
}

void PelcoDParser::reset() {
  _length = 0;
}
