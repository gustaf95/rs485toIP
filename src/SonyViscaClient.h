#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>
#include <IPAddress.h>
#include "config.h"

// Sony VISCA over IP framing (포트 52381 fallback).
// 헤더 구조 (8 bytes, big-endian): payload type(2) + payload length(2) + sequence number(4)
// 이어서 VISCA payload가 뒤따른다.
class SonyViscaClient {
 public:
  void begin();

  // VISCA payload를 Sony VISCA over IP 프레임으로 감싸 전송한다.
  bool send(const IPAddress& ip, uint16_t port, const uint8_t* viscaData, uint8_t len);

  // 카메라로부터의 응답 프레임이 도착했다면 헤더를 벗겨 VISCA payload만 꺼낸다.
  // 응답이 없으면 0을 반환한다. (Response Mode = forward에서 사용)
  uint8_t receive(uint8_t* outBuffer, uint8_t maxLen, IPAddress* remoteIp);

 private:
  WiFiUDP _udp;
  uint32_t _sequenceNumber = 1;

  static constexpr uint16_t kPayloadTypeCommand = 0x0100;
};
