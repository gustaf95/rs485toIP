#pragma once

#include <Arduino.h>
#include "config.h"

// VISCA 바이트 배열을 "81 01 06 04 FF" 형태의 HEX 문자열로 변환한다.
String viscaBytesToHex(const uint8_t* data, uint8_t len);

// 최근 RS485/IP 패킷 로그, 카운터, 오류 상태를 보관한다.
// WebConfigServer의 Dashboard/진단 화면과 USB Serial 로그가 공통으로 사용한다.
class Diagnostics {
 public:
  void begin();

  void recordRs485Rx(const uint8_t* data, uint8_t len);
  void recordIpTx(const uint8_t* data, uint8_t len, const String& target);
  void recordIgnored(const uint8_t* data, uint8_t len, const String& reason);
  void recordMalformed();
  void recordOverflow();
  void recordTimeout();
  void recordWifiReconnect();
  void recordTxError();
  void setLastRoutingResult(const String& result);

  uint32_t rs485RxCount() const { return _rs485RxCount; }
  uint32_t ipTxCount() const { return _ipTxCount; }
  uint32_t errorCount() const { return _errorCount; }
  uint32_t malformedCount() const { return _malformedCount; }
  uint32_t overflowCount() const { return _overflowCount; }
  uint32_t timeoutCount() const { return _timeoutCount; }
  uint32_t wifiReconnectCount() const { return _wifiReconnectCount; }

  const String& lastRs485Rx() const { return _lastRs485Rx; }
  const String& lastIpTx() const { return _lastIpTx; }
  const String& lastRoutingResult() const { return _lastRoutingResult; }

  String uptimeString() const;

  // 진단 화면용 최근 로그 (최신순, 최대 DIAG_LOG_DEPTH개)
  const String* rxLog() const { return _rxLog; }
  const String* txLog() const { return _txLog; }
  const String* ignoredLog() const { return _ignoredLog; }

 private:
  void pushLog(String* ring, const String& entry);

  uint32_t _rs485RxCount = 0;
  uint32_t _ipTxCount = 0;
  uint32_t _errorCount = 0;
  uint32_t _malformedCount = 0;
  uint32_t _overflowCount = 0;
  uint32_t _timeoutCount = 0;
  uint32_t _wifiReconnectCount = 0;

  String _lastRs485Rx = "-";
  String _lastIpTx = "-";
  String _lastRoutingResult = "-";

  String _rxLog[DIAG_LOG_DEPTH];
  String _txLog[DIAG_LOG_DEPTH];
  String _ignoredLog[DIAG_LOG_DEPTH];
};
