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

// millis()는 32비트라 약 49.7일마다 0으로 감긴다. 감긴 순간은 "이번 값이 지난번보다
// 작다"로만 알 수 있고, 그건 매 회전 들여다봐야 놓치지 않는다.
void Diagnostics::tickUptime() {
  uint32_t now = millis();
  if (now < _lastMillis) _millisWraps++;
  _lastMillis = now;
}

void Diagnostics::pushLog(const String& entry) {
  _recentLog[_logHead] = entry;
  _logHead = (uint8_t)((_logHead + 1) % DIAG_LOG_DEPTH);
}

void Diagnostics::pushRawLog(const String& entry) {
  _recentRawLog[_rawLogHead] = entry;
  _rawLogHead = (uint8_t)((_rawLogHead + 1) % DIAG_RAW_LOG_DEPTH);
}

// head는 다음에 쓸 칸이므로 최신은 head-1이다. index만큼 더 거슬러 올라간다.
const String& Diagnostics::recentLogAt(uint8_t index) const {
  uint8_t slot = (uint8_t)((_logHead + DIAG_LOG_DEPTH - 1 - (index % DIAG_LOG_DEPTH)) %
                            DIAG_LOG_DEPTH);
  return _recentLog[slot];
}

const String& Diagnostics::recentRawLogAt(uint8_t index) const {
  uint8_t slot = (uint8_t)((_rawLogHead + DIAG_RAW_LOG_DEPTH - 1 - (index % DIAG_RAW_LOG_DEPTH)) %
                            DIAG_RAW_LOG_DEPTH);
  return _recentRawLog[slot];
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

void Diagnostics::recordWebTx() {
  _webTx++;
}

void Diagnostics::recordWebTxDropped() {
  _webTxDropped++;
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

void Diagnostics::recordUnhandledPacket(uint8_t camNumber, const String& reason,
                                         const uint8_t* raw, uint8_t rawLen) {
  String signature = "CAM" + String(camNumber) + " | " + reason + " | " + viscaBytesToHex(raw, rawLen);

  for (uint8_t i = 0; i < _unhandledUsed; i++) {
    if (_unhandled[i].signature == signature) {
      _unhandled[i].count++;
      UnhandledEntry moved = _unhandled[i];
      for (uint8_t j = i; j > 0; j--) _unhandled[j] = _unhandled[j - 1];
      _unhandled[0] = moved;
      return;
    }
  }

  uint8_t last = (_unhandledUsed < DIAG_UNHANDLED_LOG_DEPTH) ? _unhandledUsed
                                                              : (uint8_t)(DIAG_UNHANDLED_LOG_DEPTH - 1);
  for (uint8_t j = last; j > 0; j--) _unhandled[j] = _unhandled[j - 1];
  _unhandled[0].signature = signature;
  _unhandled[0].count = 1;
  if (_unhandledUsed < DIAG_UNHANDLED_LOG_DEPTH) _unhandledUsed++;
}

String Diagnostics::unhandledEntry(uint8_t index) const {
  if (index >= _unhandledUsed) return "";
  const UnhandledEntry& e = _unhandled[index];
  if (e.count <= 1) return e.signature;
  return e.signature + "  (x" + String(e.count) + ")";
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
  _webTx = 0;
  _webTxDropped = 0;
  _malformedPacket = 0;
  _bufferOverflow = 0;
  _packetTimeout = 0;
  _wifiReconnect = 0;
}

String Diagnostics::uptimeString() const {
  // 32비트 millis()만 쓰면 49.7일에 0으로 돌아가서, 가장 오래 켜져 있었다는 걸 알고
  // 싶은 바로 그 시점에 거짓말을 한다. tickUptime()이 센 래핑 횟수를 얹어 64비트로 만든다.
  uint64_t totalMs = ((uint64_t)_millisWraps << 32) | (uint32_t)millis();
  unsigned long totalSeconds = (unsigned long)(totalMs / 1000);
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
