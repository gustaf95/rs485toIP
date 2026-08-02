#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "RoutingTable.h"
#include "Storage.h"
#include "Diagnostics.h"
#include "ViscaParser.h"
#include "PelcoDParser.h"
#include "PelcoPParser.h"
#include "Rs485Port.h"
#include "IpViscaClient.h"
#include "SonyViscaClient.h"
#include "RawBridgeClient.h"
#include "SerialMenu.h"
#include "WebConfigServer.h"
#include "StatusLed.h"

void connectWifi();

RoutingTable routingTable;
Storage storage;
Diagnostics diagnostics;
Rs485Port rs485;
ViscaParser viscaParser;
PelcoDParser pelcoDParser;
PelcoPParser pelcoPParser;
IpViscaClient ipViscaClient;
SonyViscaClient sonyViscaClient;
RawBridgeClient rawBridgeClient;
StatusLed statusLed;
SerialMenu serialMenu(routingTable, storage, diagnostics, rs485, statusLed, connectWifi);
WebConfigServer webConfigServer(routingTable, storage, diagnostics, rs485, statusLed, connectWifi);

bool wifiIsStation = false;
unsigned long lastWifiRetryMs = 0;
bool wasWifiConnected = false;
const unsigned long kWifiRetryIntervalMs = 5000;

unsigned long lastRawByteMs = 0;
bool rawLineOpen = false;
bool wasRawMonitorActive = false;

// Raw Byte Monitor 화면이 켜져 있는 동안 RS485에서 읽은 바이트를 프로토콜 파싱과
// 무관하게 그대로 hex로 echo한다. RAW_MONITOR_GAP_MS 이상 새 바이트가 없으면
// pollRawMonitor()가 줄바꿈으로 끊어서 다음 버스트를 새 줄에 보여준다.
void echoRawByte(uint8_t b) {
  if (!rawLineOpen) {
    Serial.print("[RAW] ");
    rawLineOpen = true;
  }
  if (b < 0x10) Serial.print('0');
  Serial.print(b, HEX);
  Serial.print(' ');
  lastRawByteMs = millis();
}

void pollRawMonitor() {
  if (rawLineOpen && (millis() - lastRawByteMs) > RAW_MONITOR_GAP_MS) {
    Serial.println();
    rawLineOpen = false;
  }
}

unsigned long lastDiagRawByteMs = 0;
bool diagRawLineOpen = false;
String diagRawLineBuf;

// echoRawByte()/pollRawMonitor()는 Serial Raw Byte Monitor 화면이 켜져 있을 때만
// 동작한다 (그 화면 자체가 트리거). 이건 그거랑 별개로, 화면 상태나 Input Protocol과
// 무관하게 RS485 바이트가 들어올 때마다 항상 Diagnostics의 raw 로그에 쌓아서
// WebConfigServer의 /debug/raw 폴링이 언제든 볼 게 있게 한다. 문자열 append + 링버퍼
// push라 비용이 작아 항상 켜둬도 괜찮다.
void accumulateDiagRawLog(uint8_t b) {
  if (!diagRawLineOpen) {
    diagRawLineBuf = "";
    diagRawLineOpen = true;
  }
  if (b < 0x10) diagRawLineBuf += '0';
  diagRawLineBuf += String(b, HEX);
  diagRawLineBuf += ' ';
  lastDiagRawByteMs = millis();
}

void pollDiagRawLog() {
  if (diagRawLineOpen && (millis() - lastDiagRawByteMs) > RAW_MONITOR_GAP_MS) {
    diagRawLineBuf.toUpperCase();
    diagnostics.pushRawLog(diagRawLineBuf);
    diagRawLineOpen = false;
  }
}

uint8_t rawBridgeTxBuf[RAW_BRIDGE_BUFFER_SIZE];
uint8_t rawBridgeTxLen = 0;
unsigned long rawBridgeLastByteMs = 0;

// InputProtocol::RAW_BRIDGE에서 "브릿지 피어"로 쓸 카메라 슬롯을 찾는다 - Protocol이
// RAW_DATA_UDP로 설정되어 있고 IP가 설정된 첫 번째 슬롯. 여러 슬롯에 RAW_DATA_UDP를
// 설정해도 가장 번호가 낮은 슬롯 하나만 쓰인다 (브릿지는 피어가 하나뿐이라는 전제 -
// 1:7 카메라 라우팅이 아니라 두 게이트웨이 간의 단일 링크다).
CameraSlot* findRawBridgePeer() {
  for (uint8_t camNumber = 1; camNumber <= CAMERA_SLOT_COUNT; camNumber++) {
    CameraSlot* slot = routingTable.camera(camNumber);
    if (slot->isConfigured() && slot->protocol == ProtocolMode::RAW_DATA_UDP) {
      return slot;
    }
  }
  return nullptr;
}

// 모아뒀던 raw 바이트를 UDP 한 패킷으로 피어에게 보낸다. VISCA/Pelco 파싱이 전혀
// 없으므로 체크섬 검증도, 주소 기반 라우팅도 없다 - 그냥 RS485에 흐른 바이트 그대로.
void flushRawBridgeTx() {
  if (rawBridgeTxLen == 0) return;

  SystemConfig& cfg = routingTable.get();
  diagnostics.recordRs485Rx(rawBridgeTxBuf, rawBridgeTxLen);

  CameraSlot* peer = findRawBridgePeer();
  if (!peer) {
    diagnostics.recordIgnoredNoIp(rawBridgeTxBuf, rawBridgeTxLen);
    if (cfg.debugMode) {
      Serial.println("[ACTION] Raw Bridge: no peer configured (no camera slot set to RAW_DATA_UDP) - dropped");
    }
    rawBridgeTxLen = 0;
    return;
  }

  rawBridgeClient.rebind(peer->port);
  bool ok = rawBridgeClient.send(peer->ip.toIPAddress(), peer->port, rawBridgeTxBuf, rawBridgeTxLen);
  String target = peer->ip.toIPAddress().toString() + ":" + String(peer->port);
  diagnostics.recordForwarded(rawBridgeTxBuf, rawBridgeTxLen, target);

  if (ok) {
    diagnostics.recordIpTxSuccess();
    if (cfg.debugMode) {
      Serial.print("[RAW-BRIDGE TX] ");
      Serial.print(target);
      Serial.print(" | ");
      Serial.println(viscaBytesToHex(rawBridgeTxBuf, rawBridgeTxLen));
    }
  } else {
    diagnostics.recordIpTxFailed();
    if (cfg.debugMode) {
      Serial.print("[ERROR] Raw Bridge UDP TX failed -> ");
      Serial.println(target);
    }
  }

  rawBridgeTxLen = 0;
}

void feedRawBridgeByte(uint8_t b) {
  rawBridgeTxBuf[rawBridgeTxLen++] = b;
  rawBridgeLastByteMs = millis();
  if (rawBridgeTxLen >= sizeof(rawBridgeTxBuf)) {
    flushRawBridgeTx();
  }
}

void pollRawBridgeTx() {
  if (rawBridgeTxLen > 0 && (millis() - rawBridgeLastByteMs) > RAW_BRIDGE_GAP_MS) {
    flushRawBridgeTx();
  }
}

// 피어에게서 도착한 UDP 페이로드를 그대로 RS485로 내보낸다 (반대 방향).
void pollRawBridgeRx() {
  CameraSlot* peer = findRawBridgePeer();
  if (!peer) return;
  rawBridgeClient.rebind(peer->port);

  uint8_t buf[RAW_BRIDGE_BUFFER_SIZE];
  IPAddress remoteIp;
  uint8_t len = rawBridgeClient.receive(buf, sizeof(buf), &remoteIp);
  if (len == 0) return;

  rs485.writePacket(buf, len);
  diagnostics.recordRs485TxResponse();

  SystemConfig& cfg = routingTable.get();
  if (cfg.debugMode) {
    Serial.print("[RAW-BRIDGE RX] ");
    Serial.print(remoteIp);
    Serial.print(" | ");
    Serial.println(viscaBytesToHex(buf, len));
  }
}

const char* protocolTag(ProtocolMode mode) {
  switch (mode) {
    case ProtocolMode::IP_VISCA_RAW_UDP: return "UDP";
    case ProtocolMode::IP_VISCA_RAW_TCP: return "TCP";
    case ProtocolMode::SONY_VISCA_UDP: return "SONY_UDP";
    case ProtocolMode::RAW_DATA_UDP: return "RAW_UDP";  // sendToCamera() 경로는 안 타지만 방어적으로 채워둠
  }
  return "?";
}

// Wi-Fi STA 연결을 시도한다. 실패해도 Serial 메뉴와 WebConfigServer의 AP는 계속
// 쓸 수 있다 (AP는 STA 연결 여부와 무관하게 항상 켜져 있음, WebConfigServer::begin()
// 참고) - maintainWifi()가 주기적으로 STA 재접속을 시도한다.
void connectWifi() {
  SystemConfig& cfg = routingTable.get();
  // 부팅 중 첫 호출(setup())에서는 메뉴가 잠긴 상태라 항상 조용하다. 나중에
  // "Retry Wi-Fi Connection"으로 다시 호출될 때는 메뉴가 열려 있어야만 호출 가능한
  // 동작이라 자연히 verbose해진다.
  bool verbose = serialMenu.menuActive();

  // WiFi.mode()는 SSID 유무와 상관없이 항상 먼저 호출한다 - 이것이 lwIP TCP/IP
  // 태스크를 초기화하며, 이걸 건너뛰면 이후 WiFiUDP::begin() 호출 시
  // "tcpip_send_msg_wait_sem ... Invalid mbox" assert로 재부팅 루프에 빠진다.
  // AP_STA로 하는 이유는 WebConfigServer의 AP를 STA와 동시에 띄우기 위함이다.
  WiFi.mode(WIFI_AP_STA);

  if (strlen(cfg.wifi.ssid) == 0) {
    if (verbose) {
      Serial.println("No Wi-Fi SSID configured. Use Serial menu (Network Settings) to set one.");
    }
    return;
  }

  if (!cfg.wifi.useDhcp) {
    WiFi.config(cfg.wifi.staticIp.toIPAddress(), cfg.wifi.gateway.toIPAddress(),
                cfg.wifi.subnet.toIPAddress());
  }

  if (verbose) {
    Serial.print("WiFi connecting to ");
    Serial.print(cfg.wifi.ssid);
  }
  WiFi.begin(cfg.wifi.ssid, cfg.wifi.password);
  wifiIsStation = true;

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    if (verbose) Serial.print(".");
  }
  if (verbose) Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wasWifiConnected = true;
    if (verbose) {
      Serial.print("WiFi connected: ");
      Serial.println(WiFi.localIP());
    }
  } else if (verbose) {
    Serial.println("WiFi connect failed. Will retry periodically; Serial menu remains available.");
  }
}

// STA 모드로 연결을 시도했으나 끊어진 경우, 주기적으로 재접속을 시도한다.
void maintainWifi() {
  if (!wifiIsStation) return;

  SystemConfig& cfg = routingTable.get();
  if (strlen(cfg.wifi.ssid) == 0) return;

  if (WiFi.status() == WL_CONNECTED) {
    wasWifiConnected = true;
    return;
  }

  if (wasWifiConnected) {
    if (serialMenu.menuActive()) Serial.println("WiFi disconnected, will retry");
    wasWifiConnected = false;
  }

  unsigned long now = millis();
  if (now - lastWifiRetryMs < kWifiRetryIntervalMs) return;
  lastWifiRetryMs = now;

  WiFi.begin(cfg.wifi.ssid, cfg.wifi.password);

  if (WiFi.status() == WL_CONNECTED) {
    diagnostics.recordWifiReconnect();
    if (serialMenu.menuActive()) {
      Serial.print("WiFi reconnected: ");
      Serial.println(WiFi.localIP());
    }
  }
}

bool isPanTiltStop(const uint8_t* d, uint8_t len) {
  return len >= 9 && d[1] == 0x01 && d[2] == 0x06 && d[3] == 0x01 && d[len - 3] == 0x03 &&
         d[len - 2] == 0x03;
}

bool isZoomStop(const uint8_t* d, uint8_t len) {
  return len >= 6 && d[1] == 0x01 && d[2] == 0x04 && d[3] == 0x07 && d[4] == 0x00;
}

void sendSyntheticResponse(uint8_t camNumber) {
  uint8_t replyAddr = 0x90 | camNumber;
  uint8_t ack[3] = {replyAddr, 0x41, VISCA_TERMINATOR};
  uint8_t completion[3] = {replyAddr, 0x51, VISCA_TERMINATOR};

  rs485.writePacket(ack, sizeof(ack));
  diagnostics.recordRs485TxResponse();
  rs485.writePacket(completion, sizeof(completion));
  diagnostics.recordRs485TxResponse();
}

bool sendToCamera(const CameraSlot& slot, const uint8_t* data, uint8_t len) {
  IPAddress ip = slot.ip.toIPAddress();
  switch (slot.protocol) {
    case ProtocolMode::IP_VISCA_RAW_UDP:
      return ipViscaClient.sendUdp(ip, slot.port, data, len);
    case ProtocolMode::IP_VISCA_RAW_TCP:
      return ipViscaClient.sendTcp(ip, slot.port, data, len);
    case ProtocolMode::SONY_VISCA_UDP:
      return sonyViscaClient.send(ip, slot.port, data, len);
    case ProtocolMode::RAW_DATA_UDP:
      // Raw Bridge 전용 값 - VISCA 라우팅(handleViscaPacket/forwardTranslatedVisca)이
      // 이 프로토콜의 슬롯으로 보내질 일은 없지만, 방어적으로 실패 처리한다.
      return false;
  }
  return false;
}

void handleViscaPacket(const uint8_t* data, uint8_t len) {
  SystemConfig& cfg = routingTable.get();
  diagnostics.recordRs485Rx(data, len);
  statusLed.notifyRs485Signal();

  if (cfg.debugMode) {
    Serial.print("[RX] ");
    Serial.println(viscaBytesToHex(data, len));
  }

  uint8_t addressByte = data[0];
  bool isBroadcast = (addressByte == VISCA_ADDR_BROADCAST);
  bool validStart =
      isBroadcast || (addressByte >= VISCA_ADDR_CAM1 && addressByte <= VISCA_ADDR_CAM7);

  if (!validStart) {
    diagnostics.recordMalformed();
    if (cfg.debugMode) {
      Serial.println("[ERROR] Malformed packet");
      Serial.println("[ACTION] Dropped");
    }
    return;
  }

  if ((isPanTiltStop(data, len) || isZoomStop(data, len)) && wifiIsStation &&
      WiFi.status() != WL_CONNECTED && !cfg.rs485Uart0Shared) {
    Serial.println("WARNING: Stop command received while WiFi is disconnected - may be lost");
  }

  if (isBroadcast) {
    diagnostics.recordBroadcastRx();
  }

  RoutedPacket results[CAMERA_SLOT_COUNT];
  uint8_t count = routingTable.route(data, len, results, CAMERA_SLOT_COUNT);

  if (count == 0) {
    if (isBroadcast) {
      if (cfg.debugMode) {
        Serial.println("[ROUTE] Broadcast -> no cameras configured");
        Serial.println("[ACTION] Ignored");
      }
    } else {
      diagnostics.recordIgnoredNoIp(data, len);
      if (cfg.debugMode) {
        uint8_t camNumber = addressByte - VISCA_ADDR_CAM1 + 1;
        Serial.print("[ROUTE] CAM");
        Serial.print(camNumber);
        Serial.println(" -> No IP configured");
        Serial.println("[ACTION] Ignored");
      }
    }
    return;
  }

  for (uint8_t i = 0; i < count; i++) {
    CameraSlot& slot = *results[i].slot;
    uint8_t camNumber = results[i].camNumber;
    IPAddress ip = slot.ip.toIPAddress();
    String target = ip.toString() + ":" + String(slot.port);

    if (cfg.debugMode) {
      Serial.print("[ROUTE] ");
      if (isBroadcast) {
        Serial.print("Broadcast -> CAM");
        Serial.println(camNumber);
      } else {
        Serial.print("CAM");
        Serial.print(camNumber);
        Serial.print(" -> ");
        Serial.println(target);
      }
      if (results[i].output[0] != data[0]) {
        Serial.print("[REWRITE] 0x");
        Serial.print(data[0], HEX);
        Serial.print(" -> 0x");
        Serial.println(results[i].output[0], HEX);
      }
    }

    bool ok = sendToCamera(slot, results[i].output, results[i].outputLen);

    if (!isBroadcast) {
      diagnostics.recordForwarded(results[i].output, results[i].outputLen, target);
    }

    if (ok) {
      diagnostics.recordIpTxSuccess();
      if (cfg.debugMode) {
        Serial.print("[TX] ");
        Serial.print(protocolTag(slot.protocol));
        Serial.print(" ");
        Serial.print(target);
        Serial.print(" | ");
        Serial.println(viscaBytesToHex(results[i].output, results[i].outputLen));
      }

      if (cfg.responseMode == ResponseMode::SYNTHETIC) {
        sendSyntheticResponse(camNumber);
      }
      // FORWARD / FORWARD_REWRITE 응답은 loop()의 pollCameraResponses()에서 비동기로 처리한다.
    } else {
      diagnostics.recordIpTxFailed();
      if (cfg.debugMode) {
        Serial.print("[ERROR] IP TX Failed -> ");
        Serial.println(target);
      }
    }
  }

  if (isBroadcast) {
    diagnostics.recordBroadcastForwarded(count);
  }
}

// Pelco-D DATA1/DATA2 속도 값을 VISCA VV/WW로 환산한다. 실측 전까지의 임시 정책 -
// Pelco-D 표준 관례인 0x00~0x3F(6bit) 입력 범위를 가정하고 선형 비례식으로 환산한다
// (doc/pelcoD_command.md 11절 "Pan/Tilt Speed 실제 값 범위" 항목 참고). 실측치가
// 다르면 이 함수만 교체하면 된다.
uint8_t scalePelcoSpeedToVisca(uint8_t pelcoSpeed, uint8_t viscaMax) {
  const uint8_t kPelcoSpeedMax = 0x3F;
  uint16_t scaled = ((uint16_t)pelcoSpeed * viscaMax + kPelcoSpeedMax / 2) / kPelcoSpeedMax;
  if (scaled < 1) scaled = 1;
  if (scaled > viscaMax) scaled = viscaMax;
  return (uint8_t)scaled;
}

// Pelco-D/P에서 번역된 VISCA 명령 하나를 카메라로 전달한다. handleViscaPacket()과
// 다르게 RS485로 VISCA용 합성 ACK/Completion을 돌려보내지 않는다 - Pelco 쪽 ACK은
// sendPelcoDResponse()/sendPelcoPResponse()가 이미 Pelco 포맷으로 담당하고 있어서,
// 여기서 VISCA 포맷 응답까지 또 보내면 Pelco 컨트롤러 입장에서는 알아볼 수 없는
// 바이트가 섞여 들어가게 된다.
void forwardTranslatedVisca(uint8_t camNumber, uint8_t* viscaBuf, uint8_t viscaLen,
                             const char* debugTag) {
  SystemConfig& cfg = routingTable.get();
  viscaBuf[0] = VISCA_ADDR_CAM1 + (camNumber - 1);

  RoutedPacket results[1];
  uint8_t count = routingTable.route(viscaBuf, viscaLen, results, 1);

  if (count == 0) {
    diagnostics.recordIgnoredNoIp(viscaBuf, viscaLen);
    if (cfg.debugMode) {
      Serial.print("[ROUTE] CAM");
      Serial.print(camNumber);
      Serial.println(" -> No IP configured");
      Serial.println("[ACTION] Ignored");
    }
    return;
  }

  CameraSlot& slot = *results[0].slot;
  IPAddress ip = slot.ip.toIPAddress();
  String target = ip.toString() + ":" + String(slot.port);

  if (cfg.debugMode) {
    Serial.print("[");
    Serial.print(debugTag);
    Serial.print("->VISCA] ");
    Serial.println(viscaBytesToHex(results[0].output, results[0].outputLen));
    Serial.print("[ROUTE] CAM");
    Serial.print(camNumber);
    Serial.print(" -> ");
    Serial.println(target);
  }

  bool ok = sendToCamera(slot, results[0].output, results[0].outputLen);
  diagnostics.recordForwarded(results[0].output, results[0].outputLen, target);

  if (ok) {
    diagnostics.recordIpTxSuccess();
    if (cfg.debugMode) {
      Serial.print("[TX] ");
      Serial.print(protocolTag(slot.protocol));
      Serial.print(" ");
      Serial.print(target);
      Serial.print(" | ");
      Serial.println(viscaBytesToHex(results[0].output, results[0].outputLen));
    }
  } else {
    diagnostics.recordIpTxFailed();
    if (cfg.debugMode) {
      Serial.print("[ERROR] IP TX Failed -> ");
      Serial.println(target);
    }
  }
}

// Pelco-D/P Standard/Extended Command를 VISCA 명령으로 변환해서 보낸다.
// isPelcoP로 CMND1 비트 배치 차이(Focus Near/Far 위치, doc/pelcoP_command.md 4절)만
// 분기하고, 나머지(Pan/Tilt/Zoom 비트, Preset/Query 옵코드)는 두 프로토콜이 동일한
// 값 체계를 쓰므로 공유한다 (doc/pelcoP_command.md 5절 "CMND2 값이 Pelco-D 표와
// 완전히 동일" 참고). doc/pelcoD_command.md 7.1절에서 FoMaKo 자체 지원이 확인된
// 범위만 구현한다 - Run Group/Swing, Aux, 절대좌표 Set, Focus Position Query는
// FoMaKo Pelco-D/P 표 자체에 없어서 애초에 구현 대상이 아니다 (같은 문서 10절).
void translatePelcoAndForward(uint8_t camNumber, uint8_t cmnd1, uint8_t cmnd2, uint8_t data1,
                               uint8_t data2, bool isPelcoP, const char* debugTag) {
  // Extended Command(Preset/Query)는 CMND1=0x00 + CMND2가 아래 고정 옵코드값(전부
  // 홀수)일 때만 성립한다. Standard Command 비트 플래그는 CMND2 bit0이 항상 0으로
  // 정의되어 있어(4절) 짝수이므로, 홀수 옵코드와 절대 겹치지 않는다.
  if (cmnd1 == 0x00) {
    if (cmnd2 == 0x03 || cmnd2 == 0x05 || cmnd2 == 0x07) {
      uint8_t opcode = (cmnd2 == 0x03) ? 0x01 : (cmnd2 == 0x07) ? 0x02 : 0x00;
      uint8_t buf[7] = {0, 0x01, 0x04, 0x3F, opcode, data2, VISCA_TERMINATOR};
      forwardTranslatedVisca(camNumber, buf, sizeof(buf), debugTag);
      return;
    }
    if (cmnd2 == 0x51 || cmnd2 == 0x53 || cmnd2 == 0x55) {
      // Query Pan/Tilt/Zoom Position: VISCA 조회는 비동기 응답(별도 UDP 패킷)이 와야
      // 완성되는데, 그걸 Pelco Extended Response로 재포장하는 로직은 아직 없다.
      // 요청만 받고 조용히 무시한다 (향후 작업, doc/pelcoD_command.md 10절 참고).
      return;
    }
  }

  // Stop: 전부 0. 특정 축만 지정할 방법이 없는 패킷이라, Pan/Tilt/Zoom/Focus를
  // 한꺼번에 멈춘다 - FoMaKo 자체 Pelco-D 표에도 Stop이 축 구분 없는 단일 명령으로
  // 정의되어 있어 이 방식이 실제 동작과 일치한다 (doc/pelcoD_command.md 11절).
  if (cmnd1 == 0x00 && cmnd2 == 0x00) {
    uint8_t stopPT[9] = {0, 0x01, 0x06, 0x01, 0x01, 0x01, 0x03, 0x03, VISCA_TERMINATOR};
    uint8_t stopZoom[6] = {0, 0x01, 0x04, 0x07, 0x00, VISCA_TERMINATOR};
    uint8_t stopFocus[6] = {0, 0x01, 0x04, 0x08, 0x00, VISCA_TERMINATOR};
    forwardTranslatedVisca(camNumber, stopPT, sizeof(stopPT), debugTag);
    forwardTranslatedVisca(camNumber, stopZoom, sizeof(stopZoom), debugTag);
    forwardTranslatedVisca(camNumber, stopFocus, sizeof(stopFocus), debugTag);
    return;
  }

  bool up = cmnd2 & 0x08, down = cmnd2 & 0x10, left = cmnd2 & 0x04, right = cmnd2 & 0x02;
  bool zoomTele = cmnd2 & 0x20, zoomWide = cmnd2 & 0x40;
  bool focusNear, focusFar;
  if (isPelcoP) {
    focusFar = cmnd1 & 0x01;
    focusNear = cmnd1 & 0x02;
  } else {
    focusNear = cmnd1 & 0x01;
    focusFar = cmnd2 & 0x80;
  }

  if (up || down || left || right) {
    uint8_t vv = scalePelcoSpeedToVisca(data1, 0x18);
    uint8_t ww = scalePelcoSpeedToVisca(data2, 0x14);
    uint8_t p3, p4;
    if (up && left) {
      p3 = 0x01;
      p4 = 0x01;
    } else if (up && right) {
      p3 = 0x02;
      p4 = 0x01;
    } else if (down && left) {
      p3 = 0x01;
      p4 = 0x02;
    } else if (down && right) {
      p3 = 0x02;
      p4 = 0x02;
    } else if (up) {
      p3 = 0x03;
      p4 = 0x01;
    } else if (down) {
      p3 = 0x03;
      p4 = 0x02;
    } else if (left) {
      p3 = 0x01;
      p4 = 0x03;
    } else {
      p3 = 0x02;
      p4 = 0x03;  // right
    }
    uint8_t buf[9] = {0, 0x01, 0x06, 0x01, vv, ww, p3, p4, VISCA_TERMINATOR};
    forwardTranslatedVisca(camNumber, buf, sizeof(buf), debugTag);
  }

  if (zoomTele || zoomWide) {
    uint8_t buf[6] = {0, 0x01, 0x04, 0x07, (uint8_t)(zoomTele ? 0x02 : 0x03), VISCA_TERMINATOR};
    forwardTranslatedVisca(camNumber, buf, sizeof(buf), debugTag);
  }

  if (focusNear || focusFar) {
    uint8_t buf[6] = {0, 0x01, 0x04, 0x08, (uint8_t)(focusFar ? 0x02 : 0x03), VISCA_TERMINATOR};
    forwardTranslatedVisca(camNumber, buf, sizeof(buf), debugTag);
  }
}

// Pelco-D General Response(ACK)를 합성해서 돌려준다. 체크섬은 원본 명령의 체크섬
// 바이트를 그대로 사용한다 (ALARMS=0x00이므로 sum(원본 CKSM, 0x00) = 원본 CKSM).
void sendPelcoDResponse(const uint8_t* data, uint8_t len) {
  uint8_t response[4] = {PELCO_D_START_BYTE, data[1], 0x00, data[len - 1]};
  rs485.writePacket(response, sizeof(response));
  diagnostics.recordRs485TxResponse();
}

void handlePelcoDPacket(const uint8_t* data, uint8_t len) {
  diagnostics.recordRs485Rx(data, len);
  statusLed.notifyRs485Signal();

  SystemConfig& cfg = routingTable.get();
  if (cfg.debugMode) {
    Serial.print("[PELCO-D RX] ");
    Serial.println(viscaBytesToHex(data, len));
  }

  if (cfg.pelcoResponseMode == PelcoResponseMode::SYNTHETIC) {
    sendPelcoDResponse(data, len);
    if (cfg.debugMode) {
      Serial.println("[TX] Pelco-D General Response (ACK)");
    }
  }

  // Pelco-D ADDR은 실제 주소를 그대로 쓴다 (doc/pelcoD_command.md 2절) - 카메라
  // 슬롯 1~7 밖(8 이상, 0)은 이 프로젝트의 매핑 대상이 아니라 무시한다 (8.2절).
  uint8_t camNumber = data[1];
  if (camNumber < 1 || camNumber > CAMERA_SLOT_COUNT) {
    if (cfg.debugMode) {
      Serial.println("[ACTION] Address out of range (1-7) - ignored");
    }
    return;
  }
  translatePelcoAndForward(camNumber, data[2], data[3], data[4], data[5], /*isPelcoP=*/false,
                            "PELCO-D");
}

// Pelco-P General Response(ACK)를 합성해서 돌려준다. FUJIFILM SX1600 스펙 기준
// CKSM = XOR(원본 CKSM, ALARMS=0x00) = 원본 CKSM이므로, Pelco-D와 마찬가지로
// 원본 명령의 체크섬 바이트를 그대로 재사용하면 스펙과 정확히 일치한다.
void sendPelcoPResponse(const uint8_t* data, uint8_t len) {
  uint8_t response[5] = {PELCO_P_START_BYTE, data[1], 0x00, PELCO_P_ETX_BYTE, data[len - 1]};
  rs485.writePacket(response, sizeof(response));
  diagnostics.recordRs485TxResponse();
}

void handlePelcoPPacket(const uint8_t* data, uint8_t len) {
  diagnostics.recordRs485Rx(data, len);
  statusLed.notifyRs485Signal();

  SystemConfig& cfg = routingTable.get();
  if (cfg.debugMode) {
    Serial.print("[PELCO-P RX] ");
    Serial.println(viscaBytesToHex(data, len));
  }

  if (cfg.pelcoResponseMode == PelcoResponseMode::SYNTHETIC) {
    sendPelcoPResponse(data, len);
    if (cfg.debugMode) {
      Serial.println("[TX] Pelco-P General Response (ACK)");
    }
  }

  // Pelco-P ADDR은 "실제 주소 - 1"을 wire에 싣는다 (doc/pelcoP_command.md 1/7절,
  // FUJIFILM SX1600 스펙 "ONE MINUS THE ADDRESS SET BY THE DEVICE") - Pelco-D와
  // 달리 +1 보정이 필요하다. data[1]==254/255처럼 비정상적으로 큰 값이 와도
  // camNumber가 8 이상(또는 0, uint8_t 오버플로우 시)이 되어 아래 범위 검사에서
  // 자연스럽게 걸러진다.
  uint8_t camNumber = data[1] + 1;
  if (camNumber < 1 || camNumber > CAMERA_SLOT_COUNT) {
    if (cfg.debugMode) {
      Serial.println("[ACTION] Address out of range (1-7) - ignored");
    }
    return;
  }
  translatePelcoAndForward(camNumber, data[2], data[3], data[4], data[5], /*isPelcoP=*/true,
                            "PELCO-P");
}

// 카메라로부터의 응답을 non-blocking으로 확인하여, Response Mode가 forward나
// forward_rewrite일 때 RS485로 전달한다.
void pollCameraResponses() {
  SystemConfig& cfg = routingTable.get();
  if (cfg.responseMode != ResponseMode::FORWARD && cfg.responseMode != ResponseMode::FORWARD_REWRITE) {
    return;
  }

  uint8_t buf[VISCA_BUFFER_SIZE];
  IPAddress remoteIp;

  uint8_t len = ipViscaClient.receive(buf, sizeof(buf), &remoteIp);
  if (len == 0) len = sonyViscaClient.receive(buf, sizeof(buf), &remoteIp);
  if (len == 0) return;

  for (uint8_t camNumber = 1; camNumber <= CAMERA_SLOT_COUNT; camNumber++) {
    CameraSlot* slot = routingTable.camera(camNumber);
    if (!slot->isConfigured() || slot->ip.toIPAddress() != remoteIp) continue;

    if (cfg.responseMode == ResponseMode::FORWARD_REWRITE && len > 0) {
      buf[0] = 0x90 | camNumber;
    }

    rs485.writePacket(buf, len);
    diagnostics.recordRs485TxResponse();

    if (cfg.debugMode) {
      Serial.print("Forwarded camera response from ");
      Serial.print(remoteIp);
      Serial.print(": ");
      Serial.println(viscaBytesToHex(buf, len));
    }
    return;
  }
}

void feedViscaByte(uint8_t b) {
  ViscaParseResult result = viscaParser.feed(b);
  switch (result) {
    case ViscaParseResult::PACKET_READY:
      handleViscaPacket(viscaParser.buffer(), viscaParser.length());
      viscaParser.reset();
      break;
    case ViscaParseResult::MALFORMED:
      diagnostics.recordMalformed();
      break;
    case ViscaParseResult::OVERFLOW_DISCARD:
      diagnostics.recordOverflow();
      break;
    default:
      break;
  }
}

void feedPelcoDByte(uint8_t b) {
  PelcoDParseResult result = pelcoDParser.feed(b);
  switch (result) {
    case PelcoDParseResult::PACKET_READY:
      handlePelcoDPacket(pelcoDParser.buffer(), pelcoDParser.length());
      pelcoDParser.reset();
      break;
    case PelcoDParseResult::CHECKSUM_ERROR:
      diagnostics.recordMalformed();
      break;
    default:
      break;
  }
}

void feedPelcoPByte(uint8_t b) {
  PelcoPParseResult result = pelcoPParser.feed(b);
  switch (result) {
    case PelcoPParseResult::PACKET_READY:
      handlePelcoPPacket(pelcoPParser.buffer(), pelcoPParser.length());
      pelcoPParser.reset();
      break;
    case PelcoPParseResult::CHECKSUM_ERROR:
      diagnostics.recordMalformed();
      break;
    default:
      break;
  }
}

// Pelco-D/Pelco-P 자동 판별. 이미 진행 중인 프레임이 있으면 그 파서에만 계속
// 먹인다 - 두 파서를 항상 동시에 먹이면, 진행 중인 프레임의 페이로드 바이트가
// 우연히 상대 프로토콜의 시작 바이트와 같을 때 유휴 파서가 그 자리에서
// 잘못 새 프레임을 시작해버리는 오탐(false start)이 생길 수 있다. 시작 바이트가
// 겹치지 않는다는 성질(0xFF vs 0xA0, doc/pelcoD_command.md 9.3절)은 "완전히
// 새 프레임이 시작되는 시점"에서만 안전하게 활용할 수 있다.
void feedPelcoAutoByte(uint8_t b) {
  if (pelcoDParser.length() > 0) {
    feedPelcoDByte(b);
    return;
  }
  if (pelcoPParser.length() > 0) {
    feedPelcoPByte(b);
    return;
  }

  if (b == PELCO_D_START_BYTE) {
    feedPelcoDByte(b);
  } else if (b == PELCO_P_START_BYTE) {
    feedPelcoPByte(b);
  }
  // 둘 다 아니면 노이즈 - 두 파서 모두 시작 바이트 불일치로 이미 무시한다.
}

void setup() {
  routingTable.applyDefaults();
  if (!storage.load(routingTable.get())) {
    storage.save(routingTable.get());
  }
  SystemConfig& cfg = routingTable.get();

  // RS485가 UART0을 공유하는 보드에서는 rs485.begin()이 유일하게 Serial을 시작하는
  // 주체다 - 여기서 따로 Serial.begin()을 부르면 이미 시작된 UART0을 다른 핀/설정으로
  // 다시 초기화하게 되어 충돌한다. 이 모드에서는 부팅 배너도 찍지 않는다 (Serial이
  // 콘솔이 아니라 RS485 데이터 라인이므로).
  if (!cfg.rs485Uart0Shared) {
    Serial.begin(9600);  // UART0: USB Serial 메뉴/디버그 전용
    delay(200);
    // 부팅 배너를 일부러 찍지 않는다 - 리셋 직후 Serial 메뉴가 잠금 해제(Enter 두 번)
    // 되기 전까지는 어떤 메시지도 안 보내는 게 의도다. connectWifi()/maintainWifi()도
    // 같은 이유로 메시지를 serialMenu.menuActive()로 게이팅한다.
  }

  diagnostics.begin();
  statusLed.begin(cfg.statusLedPin);

  rs485.begin(cfg.rs485Baudrate, cfg.rs485RxPin, cfg.rs485TxPin, cfg.rs485DeRePin,
              cfg.rs485Uart0Shared);

  connectWifi();

  ipViscaClient.begin();
  sonyViscaClient.begin();

  serialMenu.begin();
  webConfigServer.begin();
}

void loop() {
  serialMenu.poll();
  webConfigServer.poll();
  maintainWifi();
  statusLed.update(WiFi.status() == WL_CONNECTED);

  SystemConfig& cfg = routingTable.get();

  bool rawMonitor = serialMenu.rawMonitorActive();
  if (rawMonitor != wasRawMonitorActive) {
    // 화면을 나가고 다시 들어올 때 SerialMenu 쪽이 이미 줄바꿈/헤더를 출력해
    // 커서가 새 줄에 있으므로, rawLineOpen도 같이 초기화해 다음 echoRawByte()가
    // "[RAW] " 라벨 없이 이어붙는 걸 막는다.
    rawLineOpen = false;
    wasRawMonitorActive = rawMonitor;
  }

  while (rs485.available()) {
    uint8_t b = rs485.read();

    if (rawMonitor) {
      echoRawByte(b);
    }
    accumulateDiagRawLog(b);

    switch (cfg.inputProtocol) {
      case InputProtocol::PELCO_D:
        feedPelcoDByte(b);
        break;
      case InputProtocol::PELCO_P:
        feedPelcoPByte(b);
        break;
      case InputProtocol::PELCO_AUTO:
        feedPelcoAutoByte(b);
        break;
      case InputProtocol::RAW_BRIDGE:
        feedRawBridgeByte(b);
        break;
      case InputProtocol::VISCA:
      default:
        feedViscaByte(b);
        break;
    }
  }

  switch (cfg.inputProtocol) {
    case InputProtocol::PELCO_D:
      if (pelcoDParser.poll() == PelcoDParseResult::TIMEOUT_DISCARD) {
        diagnostics.recordTimeout();
      }
      break;
    case InputProtocol::PELCO_P:
      if (pelcoPParser.poll() == PelcoPParseResult::TIMEOUT_DISCARD) {
        diagnostics.recordTimeout();
      }
      break;
    case InputProtocol::PELCO_AUTO:
      // 자동 판별 모드에서는 둘 중 어느 쪽이 진행 중인 프레임을 갖고 있는지
      // 몰라도 안전하다 - 유휴 파서의 poll()은 length()==0이라 항상 NONE.
      if (pelcoDParser.poll() == PelcoDParseResult::TIMEOUT_DISCARD) {
        diagnostics.recordTimeout();
      }
      if (pelcoPParser.poll() == PelcoPParseResult::TIMEOUT_DISCARD) {
        diagnostics.recordTimeout();
      }
      break;
    case InputProtocol::RAW_BRIDGE:
      pollRawBridgeTx();
      break;
    case InputProtocol::VISCA:
    default:
      if (viscaParser.poll() == ViscaParseResult::TIMEOUT_DISCARD) {
        diagnostics.recordTimeout();
      }
      break;
  }

  if (rawMonitor) {
    pollRawMonitor();
  }
  pollDiagRawLog();

  if (cfg.inputProtocol == InputProtocol::RAW_BRIDGE) {
    // 피어가 보낸 데이터는 RS485 바이트 도착과 무관하게 언제든 올 수 있으므로,
    // 위 while(rs485.available()) 루프와 별개로 매 loop() 회전마다 확인한다.
    pollRawBridgeRx();
  }

  pollCameraResponses();
}
