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
  // 웹 컨트롤러가 RS485 버스로 내보낸 마스터 프레임 / 유휴 창을 못 잡아 버린 프레임.
  // rs485TxResponse(응답)와 분리한다 - 이쪽은 게이트웨이가 먼저 말을 거는 트래픽이라
  // 버스 중재가 잘 되고 있는지 판단하는 지표가 된다 (doc/Web_controller.md).
  void recordWebTx();
  void recordWebTxDropped();
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
  uint32_t webTx() const { return _webTx; }
  uint32_t webTxDropped() const { return _webTxDropped; }
  uint32_t malformedPacket() const { return _malformedPacket; }
  uint32_t bufferOverflow() const { return _bufferOverflow; }
  uint32_t packetTimeout() const { return _packetTimeout; }
  uint32_t wifiReconnect() const { return _wifiReconnect; }

  String uptimeString() const;

  // millis()의 32비트 래핑을 세어 49.7일 넘게 켜져 있어도 uptime이 맞게 한다.
  // loop()에서 매 회전 호출해야 한다 - 두 번의 호출 사이에 49.7일이 지나면 놓친다.
  void tickUptime();

  // Debug Mode 화면의 "Show Last 20 Packets"용 최근 로그.
  //
  // 링버퍼다 - index 0이 최신이고, recentLogAt()이 head에서 거꾸로 짚어준다.
  // 예전에는 배열을 통째로 한 칸씩 밀었는데(항목 하나당 String 대입 19회), 이 로그는
  // Debug Mode와 무관하게 패킷마다 채워지므로 그 이동이 재부팅 때까지 초당 수백 번씩
  // 계속됐다. head 인덱스만 옮기면 대입은 한 번이다.
  void pushLog(const String& entry);
  // index 0 = 최신. 아직 안 채워진 칸은 빈 문자열이다.
  const String& recentLogAt(uint8_t index) const;
  uint8_t recentLogDepth() const { return DIAG_LOG_DEPTH; }

  // Raw Byte Monitor(Serial/Web 공통)용 별도 링버퍼. 파싱된 RX/TX 로그와 섞이지
  // 않도록 분리했다 - main.cpp가 Input Protocol/화면 상태와 무관하게 RS485 바이트가
  // 들어올 때마다 항상 채운다(웹 페이지가 폴링할 때 데이터가 있도록).
  void pushRawLog(const String& entry);
  // index 0 = 최신.
  const String& recentRawLogAt(uint8_t index) const;
  uint8_t recentRawLogDepth() const { return DIAG_RAW_LOG_DEPTH; }

  // 라우팅 테이블 카메라 ID(1~7)로 들어왔지만 게이트웨이가 해석하지 못했거나(Unknown
  // query/set/extended command) 아직 구현하지 않은(Query Position) 패킷을 기록한다.
  // 카메라+원본 바이트가 완전히 같은 게 반복되면(컨트롤러의 폴링 재전송 등) 새 항목을
  // 추가하지 않고 기존 항목의 발생 횟수만 올린 뒤 최신순으로 끌어올린다 - 그래야 매초
  // 반복되는 같은 미해석 명령 하나가 버퍼(DIAG_UNHANDLED_LOG_DEPTH칸)를 전부
  // 잠식하지 않는다.
  void recordUnhandledPacket(uint8_t camNumber, const String& reason, const uint8_t* raw,
                              uint8_t rawLen);
  uint8_t unhandledCount() const { return _unhandledUsed; }
  // index 0이 가장 최근. 반복 횟수가 1보다 크면 "(x N)"을 덧붙인다.
  String unhandledEntry(uint8_t index) const;

 private:
  uint32_t _rs485RxTotal = 0;
  uint32_t _forwarded = 0;
  uint32_t _ignoredNoIp = 0;
  uint32_t _broadcastRx = 0;
  uint32_t _broadcastForwarded = 0;
  uint32_t _ipTxSuccess = 0;
  uint32_t _ipTxFailed = 0;
  uint32_t _rs485TxResponse = 0;
  uint32_t _webTx = 0;
  uint32_t _webTxDropped = 0;
  uint32_t _malformedPacket = 0;
  uint32_t _bufferOverflow = 0;
  uint32_t _packetTimeout = 0;
  uint32_t _wifiReconnect = 0;

  // 링버퍼. _logHead는 **다음에 쓸** 칸을 가리키므로, 최신 항목은 그 바로 앞이다.
  String _recentLog[DIAG_LOG_DEPTH];
  uint8_t _logHead = 0;
  String _recentRawLog[DIAG_RAW_LOG_DEPTH];
  uint8_t _rawLogHead = 0;

  // millis() 래핑 횟수와 직전 관측값 (tickUptime() 참고).
  uint32_t _millisWraps = 0;
  uint32_t _lastMillis = 0;

  // signature가 중복 판정 키(카메라+사유+원본 바이트, 발생 시각/횟수는 제외)를 겸한다 -
  // recordUnhandledPacket()이 이 문자열로 기존 항목을 찾는다.
  struct UnhandledEntry {
    String signature;
    uint32_t count;
  };
  UnhandledEntry _unhandled[DIAG_UNHANDLED_LOG_DEPTH];
  uint8_t _unhandledUsed = 0;
};
