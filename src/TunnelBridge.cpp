#include "TunnelBridge.h"
#include <WiFi.h>

TunnelBridge tunnelBridge;

uint32_t TunnelBridge::quietGapMs(uint32_t baud) {
  if (baud == 0) baud = 9600;
  return (40000UL / baud) + 2;  // 9600bps에서 6ms, 2400bps에서 18ms
}

bool TunnelBridge::isStop(const uint8_t* f, uint8_t len) {
  return len == PELCO_D_PACKET_LEN && f[0] == PELCO_D_START_BYTE && f[2] == 0 && f[3] == 0 &&
         f[4] == 0 && f[5] == 0;
}

bool TunnelBridge::isMovement(const uint8_t* f, uint8_t len) {
  if (len != PELCO_D_PACKET_LEN || f[0] != PELCO_D_START_BYTE) return false;
  uint8_t c1 = f[2];
  uint8_t c2 = f[3];
  // CMND2 bit0이 켜진 건 확장 명령(프리셋, EDIS C3/D3 등)이라 이동이 아니다.
  if (c2 & 0x01) return false;
  // CMND1의 Focus Near/Iris(bit0~2)와 CMND2의 Pan/Tilt/Zoom/Focus Far(bit1~7).
  return (c1 & 0x07) != 0 || (c2 & 0xFE) != 0;
}

void TunnelBridge::begin(uint16_t port) {
  _port = port;
  _udp.stop();
  _udp.begin(port);
}

bool TunnelBridge::sendTo(const IPAddress& ip, const uint8_t* data, uint8_t len) {
  // WiFi에 붙어 있지 않으면 보내지 않는다. 그 상태에서 endPacket()을 부르면 라이브러리가
  // 실패할 때마다 "could not send data: 12" 에러를 콘솔에 쏟아낸다(프레임/생존 신호마다).
  // 실패는 txFailed로 센다.
  if (WiFi.status() != WL_CONNECTED || _udp.beginPacket(ip, _port) == 0) {
    _stats.txFailed++;
    return false;
  }
  _udp.write(data, len);
  if (_udp.endPacket() == 0) {
    _stats.txFailed++;
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// 컨트롤러 쪽
// ---------------------------------------------------------------------------

void TunnelBridge::forwardFromBus(const CameraSlot& slot, const uint8_t* frame, uint8_t len) {
  if (len == 0 || len > TUNNEL_MAX_FRAME) return;
  IPAddress ip = slot.ip.toIPAddress();

  bool ok = sendTo(ip, frame, len);
  // Stop은 유실되면 카메라가 계속 도는 유일한 명령이라 한 번 더 보낸다. 같은 프레임이
  // 두 번 나가도 카메라에는 해가 없다.
  if (isStop(frame, len)) sendTo(ip, frame, len);

  if (ok) _stats.txFrames++;
  _lastForwardMs = millis();
}

void TunnelBridge::pollController(RoutingTable& routing, Rs485Port& rs485,
                                  unsigned long lastRs485ByteMs) {
  unsigned long now = millis();
  const SystemConfig& cfg = routing.get();

  // 생존 신호 - 카메라 쪽이 링크 단절을 알아채는 근거다.
  if ((now - _lastHeartbeatMs) >= TUNNEL_HEARTBEAT_MS) {
    _lastHeartbeatMs = now;
    uint8_t hb = TUNNEL_HEARTBEAT_BYTE;
    for (uint8_t i = 0; i < CAMERA_SLOT_COUNT; i++) {
      const CameraSlot& s = cfg.cameras[i];
      if (s.isTunnel()) sendTo(s.ip.toIPAddress(), &hb, 1);
    }
  }

  // 받는 쪽: 터널 슬롯의 IP에서 온 것만 받는다.
  for (uint8_t guard = 0; guard < 4; guard++) {
    int size = _udp.parsePacket();
    if (size <= 0) break;

    IPAddress from = _udp.remoteIP();
    uint8_t buf[TUNNEL_MAX_FRAME];
    int n = _udp.read(buf, (size < (int)sizeof(buf)) ? size : (int)sizeof(buf));
    if (n <= 0) continue;

    bool known = false;
    for (uint8_t i = 0; i < CAMERA_SLOT_COUNT; i++) {
      if (cfg.cameras[i].isTunnel() && cfg.cameras[i].ip.toIPAddress() == from) known = true;
    }
    if (!known) continue;
    _stats.lastPeerMs = now;

    if (n == 1 && buf[0] == TUNNEL_HEARTBEAT_BYTE) continue;
    _stats.rxFrames++;

    // 응답은 "방금 명령을 보낸 직후"에만 버스에 올린다.
    if ((now - _lastForwardMs) > TUNNEL_RELAY_WINDOW_MS || _pendingLen != 0) {
      _stats.droppedUnsolicited++;
      continue;
    }
    memcpy(_pending, buf, (size_t)n);
    _pendingLen = (uint8_t)n;
    _pendingMs = now;
  }

  if (_pendingLen == 0) return;

  if ((now - _pendingMs) > TUNNEL_RESPONSE_MAX_AGE_MS) {
    _pendingLen = 0;
    _stats.droppedStale++;
    return;
  }
  // 버스가 조용할 때만 올린다 - 송신 중에는 버스를 들을 수 없어 충돌을 감지할 수 없다.
  if ((now - lastRs485ByteMs) < quietGapMs(cfg.rs485Baudrate)) return;

  rs485.writePacket(_pending, _pendingLen);
  _stats.relayed++;
  if (_relayed) _relayed(_pending, _pendingLen);
  _pendingLen = 0;
}

// ---------------------------------------------------------------------------
// 카메라 쪽
// ---------------------------------------------------------------------------

void TunnelBridge::pollCamera(const SystemConfig& cfg, Rs485Port& rs485) {
  unsigned long now = millis();
  IPAddress peer = cfg.tunnelPeer.toIPAddress();
  bool havePeer = !cfg.tunnelPeer.isZero();

  // IP -> RS485
  for (uint8_t guard = 0; guard < 4; guard++) {
    int size = _udp.parsePacket();
    if (size <= 0) break;

    IPAddress from = _udp.remoteIP();
    uint8_t buf[TUNNEL_MAX_FRAME];
    int n = _udp.read(buf, (size < (int)sizeof(buf)) ? size : (int)sizeof(buf));
    if (n <= 0 || !havePeer || from != peer) continue;  // 정해진 상대의 것만 받는다

    _stats.lastPeerMs = now;
    _lastCommandMs = now;  // 생존 신호도 "링크가 살아 있다"는 증거다
    if (n == 1 && buf[0] == TUNNEL_HEARTBEAT_BYTE) continue;

    _stats.rxFrames++;
    if (isMovement(buf, (uint8_t)n)) {
      _moving = true;
      _movingAddr = buf[1];
    } else if (isStop(buf, (uint8_t)n)) {
      _moving = false;
    }
    rs485.writePacket(buf, (uint8_t)n);
  }

  // RS485 -> IP: 프레임 길이를 가정하지 않는다(4바이트 ACK도 7바이트 D7 응답도 있다).
  // 바이트가 끊긴 간격으로 프레임 경계를 잡는다.
  while (rs485.available()) {
    uint8_t b = rs485.read();
    // 선택 기능(기본 꺼짐): 프레임은 0xFF(Pelco-D 시작 바이트)로 시작하므로, 그 앞의
    // 바이트는 카메라가 안 물린 RX 핀이나 전기 노이즈가 만든 것으로 보고 버린다. 안 그러면
    // 노이즈 한 줄마다 UDP 패킷이 하나씩 나간다. 대신 Pelco-D가 아닌 응답도 버려진다.
    if (cfg.tunnelNoiseFilter && _rxLen == 0 && b != PELCO_D_START_BYTE) {
      _stats.noiseDiscarded++;
      continue;
    }
    if (_rxLen < sizeof(_rxBuf)) _rxBuf[_rxLen++] = b;
    _lastRxByteMs = now;
  }
  if (_rxLen > 0 && (now - _lastRxByteMs) >= quietGapMs(cfg.rs485Baudrate)) {
    if (havePeer) {
      if (sendTo(peer, _rxBuf, _rxLen)) _stats.txFrames++;
    }
    _rxLen = 0;
  }

  // 상대에게 생존 신호
  if (havePeer && (now - _lastHeartbeatMs) >= TUNNEL_HEARTBEAT_MS) {
    _lastHeartbeatMs = now;
    uint8_t hb = TUNNEL_HEARTBEAT_BYTE;
    sendTo(peer, &hb, 1);
  }

  // 안전장치: 이동 중인데 컨트롤러 쪽 소식이 끊겼다.
  if (_moving && (now - _lastCommandMs) > TUNNEL_FAILSAFE_MS) {
    uint8_t stop[PELCO_D_PACKET_LEN] = {PELCO_D_START_BYTE, _movingAddr, 0, 0, 0, 0, _movingAddr};
    rs485.writePacket(stop, sizeof(stop));
    _moving = false;
    _stats.failsafeStops++;
  }
}
