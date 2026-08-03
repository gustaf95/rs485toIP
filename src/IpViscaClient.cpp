#include "IpViscaClient.h"
#include <WiFiClient.h>

namespace {
constexpr uint32_t kTcpConnectTimeoutMs = 1000;
}  // namespace

void IpViscaClient::begin(uint16_t localPort) {
  _udp.begin(localPort);
}

bool IpViscaClient::sendUdp(const IPAddress& ip, uint16_t port, const uint8_t* data, uint8_t len) {
  if (_udp.beginPacket(ip, port) == 0) {
    return false;
  }
  _udp.write(data, len);
  return _udp.endPacket() != 0;
}

bool IpViscaClient::sendTcp(const IPAddress& ip, uint16_t port, const uint8_t* data, uint8_t len) {
  WiFiClient client;
  if (!client.connect(ip, port, kTcpConnectTimeoutMs)) {
    return false;
  }
  size_t written = client.write(data, len);
  client.stop();
  return written == len;
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
