#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>
#include <IPAddress.h>

// Raw IP VISCA: VISCA payload를 그대로 전송한다 (기본 포트 5678).
// 기본 전송은 UDP이며, UDP가 동작하지 않는 환경을 위한 TCP fallback을 함께 제공한다.
class IpViscaClient {
 public:
  void begin();

  // 성공하면 true를 반환한다.
  bool sendUdp(const IPAddress& ip, uint16_t port, const uint8_t* data, uint8_t len);

  // 매 호출마다 짧게 연결/전송/종료한다 (VISCA 명령은 짧고 빈도가 낮아
  // 연결을 유지할 필요가 없음). 성공하면 true를 반환한다.
  bool sendTcp(const IPAddress& ip, uint16_t port, const uint8_t* data, uint8_t len);

  // 카메라로부터의 응답 UDP 패킷이 도착했다면 그대로 읽어온다.
  // 응답이 없으면 0을 반환한다. (Response Mode = forward/forward_rewrite에서 사용)
  uint8_t receive(uint8_t* outBuffer, uint8_t maxLen, IPAddress* remoteIp);

 private:
  WiFiUDP _udp;
};
