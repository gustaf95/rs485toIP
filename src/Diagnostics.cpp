#include "Diagnostics.h"

String viscaBytesToHex(const uint8_t* data, uint8_t len) {
  String out;
  out.reserve(len * 3);
  for (uint8_t i = 0; i < len; i++) {
    if (data[i] < 0x10) out += '0';
    out += String(data[i], HEX);
    if (i + 1 < len) out += ' ';
  }
  out.toUpperCase();
  return out;
}

void Diagnostics::begin() {
  // 카운터/로그는 기본값(0, 빈 문자열)으로 시작한다.
}

void Diagnostics::pushLog(const String& entry) {
  for (int i = DIAG_LOG_DEPTH - 1; i > 0; i--) {
    _recentLog[i] = _recentLog[i - 1];
  }
  _recentLog[0] = entry;
}

void Diagnostics::recordRs485Rx(const uint8_t* data, uint8_t len) {
  _rs485RxTotal++;
  pushLog("[RX] " + viscaBytesToHex(data, len));
}

void Diagnostics::recordForwarded(const uint8_t* data, uint8_t len, const String& target) {
  _forwarded++;
  pushLog("[TX] " + target + " | " + viscaBytesToHex(data, len));
}

void Diagnostics::recordIgnoredNoIp(const uint8_t* data, uint8_t len) {
  _ignoredNoIp++;
  pushLog("[ACTION] Ignored (no IP): " + viscaBytesToHex(data, len));
}

void Diagnostics::recordBroadcastRx() {
  _broadcastRx++;
}

void Diagnostics::recordBroadcastForwarded(uint8_t count) {
  _broadcastForwarded += count;
}

void Diagnostics::recordIpTxSuccess() {
  _ipTxSuccess++;
}

void Diagnostics::recordIpTxFailed() {
  _ipTxFailed++;
}

void Diagnostics::recordRs485TxResponse() {
  _rs485TxResponse++;
}

void Diagnostics::recordMalformed() {
  _malformedPacket++;
}

void Diagnostics::recordOverflow() {
  _bufferOverflow++;
}

void Diagnostics::recordTimeout() {
  _packetTimeout++;
}

void Diagnostics::recordWifiReconnect() {
  _wifiReconnect++;
}

void Diagnostics::resetCounters() {
  _rs485RxTotal = 0;
  _forwarded = 0;
  _ignoredNoIp = 0;
  _broadcastRx = 0;
  _broadcastForwarded = 0;
  _ipTxSuccess = 0;
  _ipTxFailed = 0;
  _rs485TxResponse = 0;
  _malformedPacket = 0;
  _bufferOverflow = 0;
  _packetTimeout = 0;
  _wifiReconnect = 0;
}

String Diagnostics::uptimeString() const {
  unsigned long totalSeconds = millis() / 1000;
  unsigned long days = totalSeconds / 86400;
  unsigned long hours = (totalSeconds % 86400) / 3600;
  unsigned long minutes = (totalSeconds % 3600) / 60;
  unsigned long seconds = totalSeconds % 60;

  String out;
  if (days > 0) {
    out += String(days) + "d ";
  }
  char buf[16];
  snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", hours, minutes, seconds);
  out += buf;
  return out;
}
