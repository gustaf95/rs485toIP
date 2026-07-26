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
  // 카운터/로그는 기본값(0, "-")으로 시작한다.
}

void Diagnostics::pushLog(String* ring, const String& entry) {
  for (int i = DIAG_LOG_DEPTH - 1; i > 0; i--) {
    ring[i] = ring[i - 1];
  }
  ring[0] = entry;
}

void Diagnostics::recordRs485Rx(const uint8_t* data, uint8_t len) {
  _rs485RxCount++;
  _lastRs485Rx = viscaBytesToHex(data, len);
  pushLog(_rxLog, _lastRs485Rx);
}

void Diagnostics::recordIpTx(const uint8_t* data, uint8_t len, const String& target) {
  _ipTxCount++;
  _lastIpTx = viscaBytesToHex(data, len) + " -> " + target;
  pushLog(_txLog, _lastIpTx);
}

void Diagnostics::recordIgnored(const uint8_t* data, uint8_t len, const String& reason) {
  String entry = viscaBytesToHex(data, len) + " (" + reason + ")";
  pushLog(_ignoredLog, entry);
}

void Diagnostics::recordMalformed() {
  _malformedCount++;
  _errorCount++;
}

void Diagnostics::recordOverflow() {
  _overflowCount++;
  _errorCount++;
}

void Diagnostics::recordTimeout() {
  _timeoutCount++;
}

void Diagnostics::recordWifiReconnect() {
  _wifiReconnectCount++;
}

void Diagnostics::recordTxError() {
  _errorCount++;
}

void Diagnostics::setLastRoutingResult(const String& result) {
  _lastRoutingResult = result;
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
