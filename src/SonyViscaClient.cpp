#include "SonyViscaClient.h"
#include <string.h>

namespace {
constexpr uint8_t kHeaderLen = 8;

void writeU16BE(uint8_t* buf, uint16_t value) {
  buf[0] = (value >> 8) & 0xFF;
  buf[1] = value & 0xFF;
}

void writeU32BE(uint8_t* buf, uint32_t value) {
  buf[0] = (value >> 24) & 0xFF;
  buf[1] = (value >> 16) & 0xFF;
  buf[2] = (value >> 8) & 0xFF;
  buf[3] = value & 0xFF;
}

uint16_t readU16BE(const uint8_t* buf) {
  return (uint16_t(buf[0]) << 8) | buf[1];
}
}  // namespace

void SonyViscaClient::begin() {
  _udp.begin(0);
}

bool SonyViscaClient::send(const IPAddress& ip, uint16_t port, const uint8_t* viscaData, uint8_t len) {
  uint8_t frame[VISCA_BUFFER_SIZE + kHeaderLen];
  if (len > VISCA_BUFFER_SIZE) {
    return false;
  }

  writeU16BE(frame, kPayloadTypeCommand);
  writeU16BE(frame + 2, len);
  writeU32BE(frame + 4, _sequenceNumber);
  memcpy(frame + kHeaderLen, viscaData, len);

  if (_udp.beginPacket(ip, port) == 0) {
    return false;
  }
  _udp.write(frame, kHeaderLen + len);
  bool ok = _udp.endPacket() != 0;

  _sequenceNumber++;
  return ok;
}

uint8_t SonyViscaClient::receive(uint8_t* outBuffer, uint8_t maxLen, IPAddress* remoteIp) {
  int packetSize = _udp.parsePacket();
  if (packetSize < kHeaderLen) {
    return 0;
  }

  if (remoteIp) *remoteIp = _udp.remoteIP();

  uint8_t frame[VISCA_BUFFER_SIZE + kHeaderLen];
  int frameCap = (int)sizeof(frame);
  int readLen = _udp.read(frame, (packetSize < frameCap) ? packetSize : frameCap);
  if (readLen < kHeaderLen) {
    return 0;
  }

  uint16_t payloadLen = readU16BE(frame + 2);
  uint16_t available = (uint16_t)(readLen - kHeaderLen);
  uint16_t capped = (payloadLen < available) ? payloadLen : available;
  uint8_t copyLen = (capped < maxLen) ? (uint8_t)capped : maxLen;

  memcpy(outBuffer, frame + kHeaderLen, copyLen);
  return copyLen;
}
