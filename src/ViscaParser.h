#pragma once

#include <Arduino.h>
#include "config.h"

enum class ViscaParseResult {
  NONE,              // 더 받을 바이트를 기다리는 중
  PACKET_READY,      // 0xFF로 종료된 유효한 패킷이 buffer()에 있음
  MALFORMED,         // 0xFF는 받았지만 최소 길이 미만
  OVERFLOW_DISCARD,  // 버퍼 크기를 초과하여 폐기함
  TIMEOUT_DISCARD    // 패킷 도중 timeout이 발생하여 폐기함
};

// RS485 UART에서 바이트 단위로 들어오는 VISCA 스트림을 0xFF 종료 바이트 기준으로
// 하나의 패킷으로 조립한다. RS485 하드웨어 접근은 하지 않는다 (Rs485Port 담당).
class ViscaParser {
 public:
  ViscaParseResult feed(uint8_t b);

  // 진행 중인 패킷이 timeout을 초과했는지 주기적으로 확인한다.
  ViscaParseResult poll();

  // PACKET_READY 또는 MALFORMED/OVERFLOW 처리 후 다음 패킷을 위해 버퍼를 비운다.
  void reset();

  const uint8_t* buffer() const { return _buffer; }
  uint8_t length() const { return _length; }

 private:
  uint8_t _buffer[VISCA_BUFFER_SIZE];
  uint8_t _length = 0;
  unsigned long _lastByteMillis = 0;
};
