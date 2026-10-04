#pragma once

#include <Arduino.h>
#include <WiFiUdp.h>
#include <IPAddress.h>
#include "RoutingTable.h"
#include "Rs485Port.h"

// Pelco-D 터널: 번역 없이 RS485 프레임을 UDP로 실어 나르고 반대편에서 RS485로 되살린다.
//
//   컨트롤러 --RS485 버스--+-- 카메라 1,3,4 (유선)
//                          +-- [컨트롤러 쪽] ~~ UDP ~~ [카메라 쪽] --RS485-- 카메라 2
//
// 두 역할이 같은 펌웨어에 있다 (SystemConfig::tunnelRole).
//
// **컨트롤러 쪽**은 평소처럼 버스를 듣다가, 슬롯이 터널(CameraSlot::isTunnel())인 주소의
// 프레임을 번역하지 않고 그 슬롯의 IP로 보낸다. 카메라가 돌려준 응답은 "방금 그 카메라에
// 명령을 보낸 직후의 짧은 창" 안에서, 버스가 비어 있을 때만 버스에 올린다. 같은 버스에
// 1/3/4번 실물 카메라가 있어서, 질문하지 않은 응답을 아무 때나 올리면 그쪽 응답과 부딪힌다.
//
// **카메라 쪽**은 번역도 라우팅도 없이, UDP로 받은 바이트를 RS485로 내보내고 RS485에서 들은
// 바이트를 UDP로 돌려준다. 컨트롤러 쪽 소식이 끊기면(WiFi 단절) 이동 중이던 카메라에 Stop을
// 스스로 내보낸다 - 이게 이 모드에서 가장 중요한 안전장치다.
//
// 프레임은 평범한 Pelco-D 바이트 그대로라 터널 안에서 해석하지 않는다. 1바이트 0xFE 패킷만
// 생존 신호(Pelco-D 프레임은 항상 0xFF로 시작해 겹치지 않는다).
class TunnelBridge {
 public:
  struct Stats {
    uint32_t txFrames = 0;            // IP로 내보낸 프레임
    uint32_t txFailed = 0;            // 보내지 못한 것 (WiFi 미연결, 송신 버퍼 부족)
    uint32_t noiseDiscarded = 0;      // 카메라 쪽: 0xFF로 시작하지 않아 버린 바이트
    uint32_t rxFrames = 0;            // IP에서 받은 프레임 (생존 신호 제외)
    uint32_t relayed = 0;             // 컨트롤러 쪽: 버스에 올린 응답
    uint32_t droppedUnsolicited = 0;  // 컨트롤러 쪽: 질문 없이 온 응답을 버림
    uint32_t droppedStale = 0;        // 컨트롤러 쪽: 버스가 안 비어서 늦어진 응답을 버림
    uint32_t failsafeStops = 0;       // 카메라 쪽: 링크 단절로 스스로 낸 Stop
    unsigned long lastPeerMs = 0;     // 상대에게서 마지막으로 무언가 들은 시각 (0 = 아직 없음)
  };

  // 버스에 응답을 올린 직후 불린다. 웹 화면의 상태 캐시를 갱신하려고 쓴다.
  using RelayedFn = void (*)(const uint8_t* frame, uint8_t len);

  // UDP 소켓을 연다. 포트가 바뀌면 다시 불러도 된다.
  void begin(uint16_t port);
  void setRelayedHook(RelayedFn fn) { _relayed = fn; }

  // ---- 컨트롤러 쪽 ----
  // 버스에서 들은 터널 슬롯 프레임을 상대에게 보낸다.
  void forwardFromBus(const CameraSlot& slot, const uint8_t* frame, uint8_t len);
  // 터널 슬롯이 하나라도 있을 때 loop()가 매번 부른다.
  void pollController(RoutingTable& routing, Rs485Port& rs485, unsigned long lastRs485ByteMs);

  // ---- 카메라 쪽 ----
  void pollCamera(const SystemConfig& cfg, Rs485Port& rs485);

  const Stats& stats() const { return _stats; }

  // 이 시간 동안 바이트가 없으면 프레임이 끝난 것으로 본다 (보레이트 기준 4바이트 시간).
  static uint32_t quietGapMs(uint32_t baud);
  // 이동 계열 명령인가 (Stop과 확장 명령 제외). Stop이 유실되면 카메라가 계속 도는 명령들이다.
  static bool isMovement(const uint8_t* f, uint8_t len);
  static bool isStop(const uint8_t* f, uint8_t len);

 private:
  WiFiUDP _udp;
  uint16_t _port = TUNNEL_PORT_DEFAULT;
  RelayedFn _relayed = nullptr;
  Stats _stats;

  // 컨트롤러 쪽
  unsigned long _lastForwardMs = 0;
  uint8_t _pending[TUNNEL_MAX_FRAME];
  uint8_t _pendingLen = 0;
  unsigned long _pendingMs = 0;
  unsigned long _lastHeartbeatMs = 0;

  // 카메라 쪽
  uint8_t _rxBuf[TUNNEL_MAX_FRAME];
  uint8_t _rxLen = 0;
  unsigned long _lastRxByteMs = 0;
  bool _moving = false;
  uint8_t _movingAddr = 0;
  unsigned long _lastCommandMs = 0;

  bool sendTo(const IPAddress& ip, const uint8_t* data, uint8_t len);
};

extern TunnelBridge tunnelBridge;
