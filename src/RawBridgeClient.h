#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>
#include <IPAddress.h>

// Raw Bridge 모드 전용 UDP 소켓. IpViscaClient와 별도로 두는 이유는 그쪽은 송신용
// 임의 포트만 쓰는 반면, 이쪽은 피어(반대쪽 게이트웨이)가 보낸 걸 받으려면 정해진
// 로컬 포트에 "리슨"해야 하기 때문이다. 피어 슬롯의 포트가 바뀌면 rebind()로 다시
// bind해야 한다 (같은 포트로의 rebind는 아무 일도 하지 않는다).
class RawBridgeClient {
 public:
  void rebind(uint16_t localPort);

  bool send(const IPAddress& ip, uint16_t port, const uint8_t* data, uint8_t len);

  // 피어로부터의 UDP 패킷이 도착했다면 그대로 읽어온다. 없으면 0을 반환한다.
  uint8_t receive(uint8_t* outBuffer, uint8_t maxLen, IPAddress* remoteIp);

 private:
  WiFiUDP _udp;
  uint16_t _boundPort = 0;
  bool _bound = false;
};
