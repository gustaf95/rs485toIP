#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>
#include <IPAddress.h>

// Raw IP VISCA: VISCA payload를 그대로 UDP로 전송한다 (기본 포트 5678).
class IpViscaClient {
 public:
  void begin();

  // 성공하면 true를 반환한다.
  bool send(const IPAddress& ip, uint16_t port, const uint8_t* data, uint8_t len);

  // 카메라로부터의 응답 UDP 패킷이 도착했다면 그대로 읽어온다.
  // 응답이 없으면 0을 반환한다. (Response Mode = forward에서 사용)
  uint8_t receive(uint8_t* outBuffer, uint8_t maxLen, IPAddress* remoteIp);

 private:
  WiFiUDP _udp;
};
