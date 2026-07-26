#include "ViscaParser.h"

ViscaParseResult ViscaParser::feed(uint8_t b) {
  if (_length >= VISCA_BUFFER_SIZE) {
    reset();
    return ViscaParseResult::OVERFLOW_DISCARD;
  }

  _buffer[_length++] = b;
  _lastByteMillis = millis();

  if (b == VISCA_TERMINATOR) {
    if (_length >= VISCA_MIN_PACKET_LEN) {
      return ViscaParseResult::PACKET_READY;
    }
    reset();
    return ViscaParseResult::MALFORMED;
  }

  return ViscaParseResult::NONE;
}

ViscaParseResult ViscaParser::poll() {
  if (_length > 0 && (millis() - _lastByteMillis) > VISCA_PACKET_TIMEOUT_MS) {
    reset();
    return ViscaParseResult::TIMEOUT_DISCARD;
  }
  return ViscaParseResult::NONE;
}

void ViscaParser::reset() {
  _length = 0;
}
