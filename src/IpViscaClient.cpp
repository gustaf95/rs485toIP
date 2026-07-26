#include "IpViscaClient.h"

void IpViscaClient::begin() {
  _udp.begin(0);  // 임의의 로컬 포트 사용 (송신 전용)
}

bool IpViscaClient::send(const IPAddress& ip, uint16_t port, const uint8_t* data, uint8_t len) {
  if (_udp.beginPacket(ip, port) == 0) {
    return false;
  }
  _udp.write(data, len);
  return _udp.endPacket() != 0;
}

uint8_t IpViscaClient::receive(uint8_t* outBuffer, uint8_t maxLen, IPAddress* remoteIp) {
  int packetSize = _udp.parsePacket();
  if (packetSize <= 0) {
    return 0;
  }

  if (remoteIp) *remoteIp = _udp.remoteIP();

  int cap = (int)maxLen;
  int readLen = _udp.read(outBuffer, (packetSize < cap) ? packetSize : cap);
  return readLen > 0 ? (uint8_t)readLen : 0;
}
