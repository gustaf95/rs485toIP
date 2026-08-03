#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>
#include <IPAddress.h>

// Raw IP VISCA: VISCA payload를 그대로 전송한다 (기본 포트 5678).
// 기본 전송은 UDP이며, UDP가 동작하지 않는 환경을 위한 TCP fallback을 함께 제공한다.
class IpViscaClient {
 public:
  // localPort는 UDP 소켓이 bind할 로컬 포트다. 카메라 포트와 같은 값(기본 5678)을
  // 쓰는 게 안전하다 - "raw VISCA를 5678로" 방식은 표준 스펙이 아니라 벤더 관행이라
  // 카메라마다 응답을 보내는 대상이 갈리는데, 어떤 기종은 수신 패킷의 source port로
  // 되돌려주고 어떤 기종은 5678 고정으로 쏜다. 소켓을 5678에 bind해 두면 나가는
  // 패킷의 source port도 5678이 되므로 두 방식 모두 받을 수 있다.
  void begin(uint16_t localPort);

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
