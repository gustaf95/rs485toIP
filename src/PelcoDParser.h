#pragma once

#include <Arduino.h>
#include "config.h"

enum class PelcoDParseResult {
  NONE,             // 더 받을 바이트를 기다리는 중
  PACKET_READY,     // 7바이트를 모두 받았고 체크섬이 유효함
  CHECKSUM_ERROR,   // 7바이트를 받았지만 체크섬이 맞지 않음
  TIMEOUT_DISCARD   // 패킷 도중 timeout이 발생하여 폐기함
};

// UART에서 바이트 단위로 들어오는 Pelco-D 스트림을 고정 7바이트 프레임으로
// 조립한다. VISCA와 달리 종료 바이트가 없고 항상 0xFF로 시작해 길이가
// 고정(7바이트)이므로, 시작 바이트를 기다렸다가 그 뒤 6바이트를 그대로 채운다.
// RS485 하드웨어 접근은 하지 않는다 (Rs485Port 담당).
class PelcoDParser {
 public:
  PelcoDParseResult feed(uint8_t b);

  // 진행 중인 프레임이 timeout을 초과했는지 주기적으로 확인한다.
  PelcoDParseResult poll();

  // PACKET_READY 또는 CHECKSUM_ERROR 처리 후 다음 프레임을 위해 버퍼를 비운다.
  void reset();

  const uint8_t* buffer() const { return _buffer; }
  uint8_t length() const { return _length; }

 private:
  uint8_t _buffer[PELCO_D_PACKET_LEN];
  uint8_t _length = 0;
  unsigned long _lastByteMillis = 0;
};
