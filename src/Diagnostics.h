#pragma once

#include <Arduino.h>
#include "config.h"

// VISCA 바이트 배열을 "81 01 06 04 FF" 형태의 HEX 문자열로 변환한다.
String viscaBytesToHex(const uint8_t* data, uint8_t len);

// 카운터와 최근 패킷 로그를 보관한다. USB Serial 메뉴의 Counters/Debug Mode
// 화면이 이 클래스를 통해 상태를 조회한다.
class Diagnostics {
 public:
  void begin();

  void recordRs485Rx(const uint8_t* data, uint8_t len);
  void recordForwarded(const uint8_t* data, uint8_t len, const String& target);
  void recordIgnoredNoIp(const uint8_t* data, uint8_t len);
  void recordBroadcastRx();
  void recordBroadcastForwarded(uint8_t count);
  void recordIpTxSuccess();
  void recordIpTxFailed();
  void recordRs485TxResponse();
  void recordMalformed();
  void recordOverflow();
  void recordTimeout();
  void recordWifiReconnect();
  void resetCounters();

  uint32_t rs485RxTotal() const { return _rs485RxTotal; }
  uint32_t forwarded() const { return _forwarded; }
  uint32_t ignoredNoIp() const { return _ignoredNoIp; }
  uint32_t broadcastRx() const { return _broadcastRx; }
  uint32_t broadcastForwarded() const { return _broadcastForwarded; }
  uint32_t ipTxSuccess() const { return _ipTxSuccess; }
  uint32_t ipTxFailed() const { return _ipTxFailed; }
  uint32_t rs485TxResponse() const { return _rs485TxResponse; }
  uint32_t malformedPacket() const { return _malformedPacket; }
  uint32_t bufferOverflow() const { return _bufferOverflow; }
  uint32_t packetTimeout() const { return _packetTimeout; }
  uint32_t wifiReconnect() const { return _wifiReconnect; }

  String uptimeString() const;

  // Debug Mode 화면의 "Show Last 20 Packets"용 최근 로그 (최신순).
  void pushLog(const String& entry);
  const String* recentLog() const { return _recentLog; }
  uint8_t recentLogDepth() const { return DIAG_LOG_DEPTH; }

  // Raw Byte Monitor(Serial/Web 공통)용 별도 링버퍼. 파싱된 RX/TX 로그와 섞이지
  // 않도록 분리했다 - main.cpp가 Input Protocol/화면 상태와 무관하게 RS485 바이트가
  // 들어올 때마다 항상 채운다(웹 페이지가 폴링할 때 데이터가 있도록).
  void pushRawLog(const String& entry);
  const String* recentRawLog() const { return _recentRawLog; }
  uint8_t recentRawLogDepth() const { return DIAG_RAW_LOG_DEPTH; }

 private:
  uint32_t _rs485RxTotal = 0;
  uint32_t _forwarded = 0;
  uint32_t _ignoredNoIp = 0;
  uint32_t _broadcastRx = 0;
  uint32_t _broadcastForwarded = 0;
  uint32_t _ipTxSuccess = 0;
  uint32_t _ipTxFailed = 0;
  uint32_t _rs485TxResponse = 0;
  uint32_t _malformedPacket = 0;
  uint32_t _bufferOverflow = 0;
  uint32_t _packetTimeout = 0;
  uint32_t _wifiReconnect = 0;

  String _recentLog[DIAG_LOG_DEPTH];
  String _recentRawLog[DIAG_RAW_LOG_DEPTH];
};
