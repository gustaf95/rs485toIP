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

// 카메라의 노출/화이트밸런스/포커스 모드를 VISCA 코드 그대로 캐시해둔다. Pelco
// 컨트롤러의 D3 19 상태 조회에 답하려면 이 값이 필요한데, VISCA 조회 응답은 비동기로
// 오므로 미리 받아둬야 한다.
//
// 초기값은 전부 Auto다 - 부팅 직후 아직 아무것도 조회하지 못한 상태에서도 컨트롤러에
// 그럴듯한 답을 주고, 실제 값은 pollCameraModeInquiries()가 따라잡는다. C3 SET이
// 지나갈 때는 방금 무엇으로 바꿨는지 알 수 있으므로 그 자리에서 낙관적으로 갱신해,
// 조회 주기를 기다리지 않고 즉시 반영한다.
struct CameraModeCache {
  uint8_t power;      // VISCA CAM_Power:        0x02 On, 0x03 Standby
  uint8_t aeMode;     // VISCA CAM_AEMode:       0x00 Full Auto, 0x03 Manual, 0x0A/0x0B 우선순위 모드
  uint8_t wbMode;     // VISCA CAM_WBMode:       0x00 Auto, 0x05 Manual, ...
  uint8_t focusMode;  // VISCA CAM_FocusAFMode:  0x02 Auto, 0x03 Manual, 0x04 One Push
  uint8_t backlight;  // VISCA CAM_Back Light:   0x02 On, 0x03 Off
};
// 초기값 = 전부 Auto (VISCA 코드로 AE Full Auto / WB Auto / Focus Auto).
CameraModeCache modeCache[CAMERA_SLOT_COUNT] = {};
void resetModeCache() {
  for (uint8_t i = 0; i < CAMERA_SLOT_COUNT; i++) {
    modeCache[i].power = 0x02;      // On
    modeCache[i].aeMode = 0x00;     // Full Auto
    modeCache[i].wbMode = 0x00;     // Auto
    modeCache[i].focusMode = 0x02;  // Auto Focus
    // BLC만 Auto 계열 기본값이 없다. ED-P 매뉴얼의 공장 초기값이 OFF라 그쪽을 따른다.
    modeCache[i].backlight = 0x03;  // Back Light Off
  }
}

// ---------------------------------------------------------------------------
// Auto Power Control
// ---------------------------------------------------------------------------
// 카메라별 슬롯 옵션(CameraSlot::autoPowerControl)이 켜졌을 때, RS485 버스가 얼마나
// "재잘거리는지"만 보고 카메라 전원을 자동으로 켜고/대기시킨다.
//
// target(목표)과 confirmed(실측 확인) 상태를 따로 둔다 - updateAutoPowerFromChatter()가
// target을 정하고, reconcileAutoPower()가 target에 맞는 VISCA 명령을 보낸 뒤 실측으로
// confirmed를 따라잡을 때까지 재시도한다. 명령을 보낸 순간 낙관적으로 상태를 갱신하지
// 않는 이유는, 카메라가 명령을 거부하거나(예: 이미 그 상태) 아예 응답하지 않는 경우를
// "실제로 그렇게 됐다"고 잘못 믿지 않기 위해서다.
enum class AutoPowerState : uint8_t { UNKNOWN = 0, ON = 1, OFF = 2 };
AutoPowerState autoPowerConfirmed[CAMERA_SLOT_COUNT] = {};  // CAM_PowerInq 응답으로만 갱신
AutoPowerState autoPowerTarget[CAMERA_SLOT_COUNT] = {};     // UNKNOWN = 아직 청취 판정 전, 목표 없음
// false = 다음 reconcile에서 CAM_Power 명령을 보낼 차례, true = 명령을 보내고 나서
// AUTO_POWER_VERIFY_DELAY_MS 뒤 CAM_PowerInq로 확인할 차례.
bool autoPowerAwaitingVerify[CAMERA_SLOT_COUNT] = {};
unsigned long autoPowerActionDueMs[CAMERA_SLOT_COUNT] = {};  // 다음 송신/확인을 시도할 시각
// 현재 재시도 간격(backoff) - 실패할 때마다 두 배로 늘어 AUTO_POWER_RETRY_MAX_DELAY_MS에서
// 멈춘다. 목표에 도달하면(confirmed==target) AUTO_POWER_VERIFY_DELAY_MS로 되돌아간다.
unsigned long autoPowerRetryDelayMs[CAMERA_SLOT_COUNT] = {};

void resetAutoPowerControl() {
  for (uint8_t i = 0; i < CAMERA_SLOT_COUNT; i++) {
    autoPowerConfirmed[i] = AutoPowerState::UNKNOWN;
    autoPowerTarget[i] = AutoPowerState::UNKNOWN;
    autoPowerAwaitingVerify[i] = false;
    autoPowerActionDueMs[i] = 0;
    autoPowerRetryDelayMs[i] = AUTO_POWER_VERIFY_DELAY_MS;
  }
}

// 텀블링 10초 윈도우 안에서 관측된 유효 패킷 수. handleViscaPacket/handlePelcoDPacket/
// handlePelcoPPacket이 체크섬까지 통과한 패킷마다(카메라 ID/슬롯 설정 여부와 무관하게)
// 호출한다. RAW_BRIDGE 입력에서는 이 핸들러들 자체가 호출되지 않으므로(main.cpp
// loop()의 InputProtocol 분기 참고) 카운트가 항상 0으로 남는다.
uint16_t autoPowerChatterCount = 0;
unsigned long autoPowerWindowStartMs = 0;

void recordAutoPowerChatterPacket() {
  if (autoPowerChatterCount < 0xFFFF) autoPowerChatterCount++;
}

// 지금 답을 기다리는 중인 조회. VISCA 조회 응답이 전부 `y0 50 pp FF`로 똑같이 생겨서,
// 어느 질문의 답인지는 "무엇을 물었는지"를 기억하는 것으로만 알 수 있다.
enum class ModeInquiry : uint8_t { NONE = 0, POWER, AE, WB, FOCUS, BACKLIGHT };
#define MODE_INQUIRY_ITEM_COUNT 5
ModeInquiry pendingInquiry = ModeInquiry::NONE;
uint8_t pendingInquiryCam = 0;
unsigned long lastInquiryMs = 0;
uint8_t inquiryCamCursor = 1;   // 1~7
uint8_t inquiryItemCursor = 0;  // kInquiryCodes / ModeInquiry 열거와 같은 순서

// C3 SET이 지나가면 그 항목을 다음 조회로 예약한다 - 라운드로빈 한 바퀴를 기다리지 않고
// 바로 확인해서, 카메라가 명령을 거부했을 때 컨트롤러 표시가 오래 거짓말하지 않게 한다.
uint8_t priorityInquiryCam = 0;  // 0이면 예약 없음
uint8_t priorityInquiryItem = 0;
unsigned long priorityInquiryDueMs = 0;

// One Push AF를 트리거한 뒤 Focus 모드를 Manual로 되돌릴 예약
// (pollOnePushAfRestore() 참고). 0이면 예약 없음.
uint8_t onePushRestoreCam = 0;
unsigned long onePushRestoreDueMs = 0;

// 직전에 카메라로 내보낸 번역 결과 - 같은 명령이 연달아 쏟아지는 걸 억제하는 데 쓴다
// (isDuplicateViscaCommand() 참고).
uint8_t lastSentCam = 0;
uint8_t lastSentBuf[VISCA_BUFFER_SIZE];
uint8_t lastSentLen = 0;
unsigned long lastSentMs = 0;

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

// Raw Byte Monitor에 게이트웨이가 송신한 패킷을 표시한다 (Rs485Port의 TxEcho 훅).
//
// echoRawByte()는 RS485에서 "읽어들인" 바이트만 보여주는데, 게이트웨이는 자기가 보낸
// 바이트를 자기 RX로 되들을 수 없다 - writePacket()이 DE/RE를 송신 쪽으로 올리는 동안
// 트랜시버의 수신부가 꺼지기 때문이다. 그래서 이 훅이 없으면 우리 응답만 화면에서 통째로
// 사라져, 다른 카메라 응답은 보이는데 6번(게이트웨이) 응답만 없는 것처럼 보인다.
//
// 수신 줄과 섞이지 않도록 열려 있던 [RAW] 줄을 먼저 끊고 별도 라벨로 한 줄에 찍는다.
void echoRawTxPacket(const uint8_t* data, uint8_t len) {
  if (!serialMenu.rawMonitorActive()) return;

  if (rawLineOpen) {
    Serial.println();
    rawLineOpen = false;
  }
  Serial.print("[TX ] ");
  for (uint8_t i = 0; i < len; i++) {
    if (data[i] < 0x10) Serial.print('0');
    Serial.print(data[i], HEX);
    Serial.print(' ');
  }
  Serial.println();
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
  recordAutoPowerChatterPacket();

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

// Pelco-D DATA1/DATA2 속도 값을 VISCA VV/WW로 환산한다. Pelco-D 표준 관례인
// 0x00~0x3F(6bit) 입력 범위를 선형 비례식으로 환산한다 (doc/pelcoD_command.md 11절
// "Pan/Tilt Speed 실제 값 범위" 항목 참고). ZU-EPC7000이 조이스틱 최대 변위에서 팬/틸트
// 양쪽 모두 0x3F를 보내는 것이 실측으로 확인됐다(2026-08-08) - 축별로 다른 만점을 쓸
// 필요가 없어 kPelcoSpeedMax 하나를 공유한다. 다른 컨트롤러에서 범위가 다르면 이 함수만
// 교체하면 된다.
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
// 직전에 보낸 것과 바이트 단위로 완전히 같은 명령이 억제 창 안에 또 들어왔는지 판정한다.
//
// Pelco 컨트롤러는 조이스틱을 물고 있는 동안 같은 프레임을 초당 수십 번 계속 재전송한다.
// 반면 VISCA의 Pan-tiltDrive는 래치 방식이라 한 번 보내면 Stop이 올 때까지 그대로 도는
// 명령이다. 그래서 그 재전송을 그대로 UDP로 흘리면 카메라 명령 큐가 밀리고, 조이스틱을
// 놓아도 밀린 명령이 다 소화될 때까지 카메라가 계속 흘러간다.
//
// 방향이나 속도가 조금이라도 바뀌면 바이트가 달라져 즉시 통과하므로, Stop을 포함해
// "새로운 명령"이 지연되는 일은 없다.
bool isDuplicateViscaCommand(uint8_t camNumber, const uint8_t* buf, uint8_t viscaLen) {
  if (camNumber != lastSentCam || viscaLen != lastSentLen ||
      memcmp(buf, lastSentBuf, viscaLen) != 0) {
    return false;
  }
  return (millis() - lastSentMs) < VISCA_DUPLICATE_SUPPRESS_MS;
}

void rememberSentViscaCommand(uint8_t camNumber, const uint8_t* buf, uint8_t viscaLen) {
  lastSentCam = camNumber;
  lastSentLen = (viscaLen < VISCA_BUFFER_SIZE) ? viscaLen : (uint8_t)VISCA_BUFFER_SIZE;
  memcpy(lastSentBuf, buf, lastSentLen);
  lastSentMs = millis();
}

// isRelativeStep = "같은 바이트의 반복이 곧 의미인" 명령(R/B Gain, ExpComp의 Up/Down)일 때
// true. 중복 억제를 건너뛴다.
//
// 중복 억제는 래치 명령(Pan-tiltDrive)을 위해 만든 장치다 - 조이스틱을 물고 있는 동안
// 컨트롤러가 같은 프레임을 초당 수십 번 재전송하는데, 래치 명령은 한 번 보내면 Stop이
// 올 때까지 유지되므로 재전송이 전부 군더더기다. 그 전제가 상대 조정에는 성립하지 않는다:
// `04 0E 02`를 두 번 보내는 건 "같은 말을 두 번"이 아니라 "두 칸 올려라"다. 억제하면
// 200ms에 한 칸씩만 통과해, 키를 눌러도 화면이 안 움직이는 것처럼 보인다.
//
// 흥미롭게도 이 문제는 게이트웨이 슬롯에만 생긴다 - 같은 버스의 실물 ED-P는 컨트롤러
// 패킷을 직접 받으므로 억제를 거치지 않는다. "3번은 되는데 6번은 안 된다"의 원인이 될 수 있다.
void forwardTranslatedVisca(uint8_t camNumber, uint8_t* viscaBuf, uint8_t viscaLen,
                             const char* debugTag, bool isRelativeStep = false) {
  SystemConfig& cfg = routingTable.get();
  viscaBuf[0] = VISCA_ADDR_CAM1 + (camNumber - 1);

  if (!isRelativeStep && isDuplicateViscaCommand(camNumber, viscaBuf, viscaLen)) {
    if (cfg.debugMode) {
      Serial.print("[");
      Serial.print(debugTag);
      Serial.println("] Duplicate command suppressed");
    }
    return;
  }
  rememberSentViscaCommand(camNumber, viscaBuf, viscaLen);

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

// EDIS C3 SET이 지나갈 때 캐시를 낙관적으로 갱신한다 - 방금 무엇으로 바꿨는지 아니까,
// VISCA 조회 주기를 기다리지 않고 다음 D3 19 조회에 바로 반영할 수 있다. 카메라가
// 명령을 거부해서 실제 값이 다르더라도 pollCameraModeInquiries()가 곧 바로잡는다.
void updateModeCacheFromSet(uint8_t camNumber, uint8_t viscaCode, uint8_t value) {
  if (camNumber < 1 || camNumber > CAMERA_SLOT_COUNT) return;
  CameraModeCache& c = modeCache[camNumber - 1];
  uint8_t item;
  switch (viscaCode) {
    case VISCA_CAM_POWER: c.power = value; item = 0; break;
    case 0x39: c.aeMode = value; item = 1; break;
    case VISCA_CAM_WB_MODE: c.wbMode = value; item = 2; break;
    case VISCA_CAM_FOCUS_AF_MODE: c.focusMode = value; item = 3; break;
    case VISCA_CAM_BACKLIGHT: c.backlight = value; item = 4; break;
    // 캐시에 담을 모드 상태가 없는 파라미터들 - One Push AF(0x18) 같은 일회성 트리거와,
    // R/B Gain·ExpComp(0x03/0x04/0x0E) 같은 상대 조정(Up/Down)이 여기로 온다. 확인 조회도
    // 예약하지 않는다: 물어볼 "설정한 값"이 애초에 없고, 컨트롤러도 그 값을 표시하지 않는다.
    default: return;
  }

  // 방금 설정한 항목을 곧바로 되물어 실제로 적용됐는지 확인한다.
  priorityInquiryCam = camNumber;
  priorityInquiryItem = item;
  priorityInquiryDueMs = millis() + MODE_INQUIRY_SET_VERIFY_DELAY_MS;
}

// 게이트웨이가 EDIS 응답을 RS485로 내보낼 때 그 바이트를 그대로 로그에 남긴다.
//
// 이게 없으면 응답이 나가는지 눈으로 확인할 방법이 아예 없다 - writePacket()이 DE/RE를
// 송신 쪽으로 올리는 동안 트랜시버의 수신부가 꺼지므로, 게이트웨이는 자기가 보낸 바이트를
// 자기 Raw Byte Monitor로 다시 들을 수 없다. 다른 카메라의 응답은 같은 줄에 보이는데
// 우리 응답만 안 보이는 게 그 때문이다.
void logEdisResponse(const char* what, const uint8_t* resp, uint8_t len) {
  if (!routingTable.get().debugMode) return;
  Serial.print("[TX] ");
  Serial.print(what);
  Serial.print(" | ");
  Serial.println(viscaBytesToHex(resp, len));
}

// D3 04 <item>(단일 항목 조회)에 대한 D7 응답. 어떤 항목이든 배치가 같다 - 모드 상태
// 조회(D3 19)와 달리 RESP1에 데이터가 실리지 않고 CMND1(0x00)이 그대로 에코되며,
// DATA2가 VISCA 값 코드(0x02/0x03)를 그대로 담는다.
//
// 응답만 봐서는 어느 항목의 답인지 구분되지 않는다 - 실측한 CAM_Power(`D3 04 00`)와
// CAM_Back Light(`D3 04 33`) 응답이 바이트 배치까지 완전히 같았다. 컨트롤러가 질문 순서로
// 짝을 맞추는 구조라, 우리도 받은 순서대로 그 자리에서 답하면 된다.
void sendEdisItemStatus(uint8_t camNumber, uint8_t value, const char* what) {
  uint8_t resp[PELCO_D_PACKET_LEN] = {PELCO_D_START_BYTE, camNumber, 0x00,
                                      PELCO_EDIS_QUERY_RESPONSE, 0x00, value, 0};
  uint8_t sum = 0;
  for (uint8_t i = 1; i < PELCO_D_PACKET_LEN - 1; i++) sum += resp[i];
  resp[PELCO_D_PACKET_LEN - 1] = sum;

  rs485.writePacket(resp, sizeof(resp));
  diagnostics.recordRs485TxResponse();
  logEdisResponse(what, resp, sizeof(resp));
}

// D3 19 상태 조회에 대한 D7 응답을 캐시에서 조립해 RS485로 돌려준다.
// 규격은 실측으로 복원했다 (doc/pelcoD_command.md 참고):
//
//   FF ADDR R1 D7 19 D2 CK
//     R1 = 0x50 | (VISCA WB 모드 코드 & 0x0F)
//     D2 = (Iris가 Auto가 아니면 0x40) | (Focus가 Auto가 아니면 0x01)
//     CK = ADDR..D2 합
void sendEdisModeStatus(uint8_t camNumber) {
  const CameraModeCache& c = modeCache[camNumber - 1];

  // AWB는 VISCA WB 모드 코드가 그대로 실린다 - 실측에서 Manual이 0x05, Auto가 0x00으로
  // VISCA CAM_WBModeInq 반환값과 정확히 같았다.
  uint8_t r1 = PELCO_EDIS_STATUS_RESP1_BASE | (c.wbMode & 0x0F);

  // Iris/Focus는 값 코드가 아니라 단순 Manual 플래그다. 실측 범위가 Auto/Manual 두
  // 가지뿐이라, "Auto가 아니면 Manual"로 접어서 보낸다 - 컨트롤러 화면 자체가
  // Auto/Manual 이분법이므로 Shutter/Iris 우선순위 같은 중간 모드도 Manual로 보이는
  // 게 오히려 사실에 가깝다.
  uint8_t d2 = 0;
  if (c.aeMode != 0x00) d2 |= PELCO_EDIS_STATUS_IRIS_MANUAL;
  if (c.focusMode != 0x02) d2 |= PELCO_EDIS_STATUS_FOCUS_MANUAL;

  uint8_t resp[PELCO_D_PACKET_LEN] = {PELCO_D_START_BYTE, camNumber, r1,
                                      PELCO_EDIS_QUERY_RESPONSE, PELCO_EDIS_QUERY_MODE_STATUS,
                                      d2, 0};
  uint8_t sum = 0;
  for (uint8_t i = 1; i < PELCO_D_PACKET_LEN - 1; i++) sum += resp[i];
  resp[PELCO_D_PACKET_LEN - 1] = sum;

  rs485.writePacket(resp, sizeof(resp));
  diagnostics.recordRs485TxResponse();
  logEdisResponse("Mode status", resp, sizeof(resp));
}

// EDIS 벤더 확장(C3 SET / D3 GET)을 처리한다.
void handleEdisVendorCommand(uint8_t camNumber, uint8_t cmnd2, uint8_t data1, uint8_t data2,
                              const uint8_t* rawPacket, uint8_t rawLen, const char* debugTag) {
  SystemConfig& cfg = routingTable.get();

  if (cmnd2 == PELCO_EDIS_QUERY_CMD) {
    // 조회에 답하는 건 이 게이트웨이가 담당하는 슬롯 - IP가 설정된 주소 - 뿐이다.
    // IP가 비어 있는 주소는 같은 버스의 실물 ED-P 카메라 몫이고, 그쪽이 이미
    // 스스로 답하고 있으므로 여기서 끼어들면 드라이버 두 개가 충돌한다.
    //
    // pelcoResponseMode(합성 ACK 설정)와는 무관하게 동작한다. 그건 "명령을 받았다"는
    // General Response를 지어낼지에 대한 설정이고, 이쪽은 컨트롤러가 명시적으로 값을
    // 물어본 데 대한 데이터 응답이라 성격이 다르다. 충돌 위험은 위의 슬롯 검사로
    // 이미 막혀 있다.
    CameraSlot* slot = routingTable.camera(camNumber);
    if (slot == nullptr || !slot->isConfigured()) return;

    // 카메라가 자고 있으면 조회에 답하지 않는다 - 단, **전원 조회(`D3 04 00`)만은
    // 예외로 답한다.**
    //
    // 예전엔 스탠바이 중 모든 조회에 침묵했다. "실물 ED-P도 그렇게 한다"는 게 근거였는데
    // 실측이 아닌 추정이었고, 틀린 것으로 확인됐다(2026-08-12). 잠든 3번 ED-P가 전원
    // 조회에 자기 상태를 정직하게 돌려준다:
    //
    //   FF 03 00 D3 04 00 DA  ->  FF 03 00 D7 00 03 DD      값 03 = Standby
    //
    // 하필 전원 조회가 "너 켜져 있냐"는 질문 그 자체라, 여기에 침묵하면 컨트롤러는
    // "자는 중"과 "그 주소에 아무것도 없음"을 구분할 수 없다. 답할 값(0x03)을 캐시에
    // 이미 갖고 있으면서 알려주지 않을 이유가 없다.
    //
    // 나머지 조회(D3 19 모드 상태, D3 04 33 BLC)는 침묵을 유지한다 - 잠든 동안 그
    // 값들은 의미가 없고, 실물이 그때 무엇을 답하는지 아직 실측하지 않았다.
    //
    // 전원 상태는 VISCA CAM_PowerInq(`09 04 00`)를 주기적으로 던져 확인하고
    // (pollCameraModeInquiries), 컨트롤러가 C3 00으로 스탠바이를 명령하면 그 자리에서
    // 바로 반영된다 - 후자 덕분에 카메라가 잠든 채 CAM_PowerInq에 답하지 않더라도
    // 캐시 값은 정확하다.
    bool isPowerQuery = (data1 == PELCO_EDIS_QUERY_ITEM && data2 == VISCA_CAM_POWER);
    if (modeCache[camNumber - 1].power != 0x02 && !isPowerQuery) {
      if (cfg.debugMode) {
        Serial.print("[");
        Serial.print(debugTag);
        Serial.println("] Query while camera is in standby - no answer");
      }
      return;
    }

    if (data1 == PELCO_EDIS_QUERY_MODE_STATUS) {
      sendEdisModeStatus(camNumber);
      if (cfg.debugMode) {
        Serial.print("[");
        Serial.print(debugTag);
        Serial.println("] Mode status query -> answered from cache");
      }
      return;
    }
    // 단일 항목 조회. **DATA2가 어느 항목인지를 정한다** - DATA1만 보고 답하면 안 된다.
    // 예전엔 DATA2가 항상 0x00이라 이 조회 자체를 "전원 조회"로 봤는데, BACK LIGHT 키가
    // `D3 04 33`을 보내는 게 확인되면서(2026-08-08) DATA2가 항목 선택자임이 드러났다.
    // DATA1만 보고 분기하면 BLC를 물었는데 전원 상태로 답하게 된다.
    if (data1 == PELCO_EDIS_QUERY_ITEM) {
      const CameraModeCache& c = modeCache[camNumber - 1];
      if (data2 == VISCA_CAM_POWER) {
        sendEdisItemStatus(camNumber, c.power, "Power status");
      } else if (data2 == VISCA_CAM_BACKLIGHT) {
        sendEdisItemStatus(camNumber, c.backlight, "Back light status");
      } else {
        // 모르는 항목에 값을 지어내면 컨트롤러가 거짓을 표시한다. 침묵하고 기록만 한다.
        if (cfg.debugMode) {
          Serial.print("[");
          Serial.print(debugTag);
          Serial.print("] Unknown query item 0x04/0x");
          Serial.print(data2, HEX);
          Serial.println(" - no answer");
        }
        char reason[40];
        snprintf(reason, sizeof(reason), "Unknown query item 0x04/0x%02X", data2);
        diagnostics.recordUnhandledPacket(camNumber, reason, rawPacket, rawLen);
        return;
      }
      if (cfg.debugMode) {
        Serial.print("[");
        Serial.print(debugTag);
        Serial.print("] Item query 0x");
        Serial.print(data2, HEX);
        Serial.println(" -> answered from cache");
      }
      return;
    }
    // 해독하지 못한 조회 항목. 억지로 답을 지어내면 컨트롤러가 잘못된 값을 표시하게
    // 되므로 침묵한다 - 실물 카메라가 응답하지 않을 때와 같은 상태가 된다.
    if (cfg.debugMode) {
      Serial.print("[");
      Serial.print(debugTag);
      Serial.print("] Unknown query item 0x");
      Serial.print(data1, HEX);
      Serial.println(" - no answer");
    }
    {
      char reason[32];
      snprintf(reason, sizeof(reason), "Unknown query item 0x%02X", data1);
      diagnostics.recordUnhandledPacket(camNumber, reason, rawPacket, rawLen);
    }
    return;
  }

  // SET: DATA1/DATA2가 VISCA `01 04 pp qq`의 pp/qq와 **대체로** 같다 - AWB(0x36)와
  // One Push AF(0x18)는 예외라 아래에서 따로 재매핑한다(config.h 참고).
  //
  // 실측으로 확인된 파라미터만 통과시킨다 - 모르는 pp를 그대로 흘리면 엉뚱한 VISCA
  // 명령이 만들어져 카메라가 예상 못 한 동작을 할 수 있다. 반대로 이 목록이 실제
  // 컨트롤러가 보내는 값과 어긋나면 그 키가 통째로 먹지 않는다 - AWB가 0x35로 잘못
  // 적혀 있어 실제로 그랬다(2026-08-12).
  bool known = (data1 == VISCA_CAM_POWER ||  // CAM_Power        (02 On / 03 Standby)
                data1 == 0x39 ||   // CAM_AEMode       (00 Full Auto / 03 Manual)
                data1 == VISCA_CAM_FOCUS_AF_MODE ||  // (02 Auto / 03 Manual / 04 One Push)
                data1 == PELCO_EDIS_SET_WB_MODE ||   // AWB (00 Auto / 05 Manual, 아래에서 재매핑)
                data1 == VISCA_CAM_BACKLIGHT ||  // CAM_Back Light (02 On / 03 Off)
                // 아래 셋은 절대 모드가 아니라 상대 조정이다 - 02 Up / 03 Down.
                // 여기서 02/03은 다른 파라미터의 On/Off, Auto/Manual과 뜻이 다르다.
                data1 == VISCA_CAM_RGAIN ||      // CAM_RGain  (02 Up / 03 Down)
                data1 == VISCA_CAM_BGAIN ||      // CAM_BGain  (02 Up / 03 Down)
                data1 == VISCA_CAM_SHUTTER ||    // CAM_Shutter (02 Up / 03 Down)
                data1 == VISCA_CAM_EXP_COMP ||   // CAM_ExpComp, CAM_Bright(0x0D) 아님
                data1 == PELCO_EDIS_SET_FOCUS_TRIGGER);  // One Push AF (아래에서 재매핑)
  if (!known) {
    if (cfg.debugMode) {
      Serial.print("[");
      Serial.print(debugTag);
      Serial.print("] Unknown set parameter 0x");
      Serial.print(data1, HEX);
      Serial.println(" - ignored");
    }
    {
      char reason[32];
      snprintf(reason, sizeof(reason), "Unknown set parameter 0x%02X", data1);
      diagnostics.recordUnhandledPacket(camNumber, reason, rawPacket, rawLen);
    }
    return;
  }

  // AWB는 pp 자체가 VISCA 코드가 아니다 - 컨트롤러는 0x36을 보내는데 VISCA CAM_WB는
  // 0x35다(실측 2026-08-12, config.h의 PELCO_EDIS_SET_WB_MODE 참고). qq는 VISCA 값
  // 그대로(00 Auto / 05 Manual)라 코드 한 바이트만 갈아끼우면 된다.
  //
  // 여기서 미리 바꿔두면 아래 전송과 updateModeCacheFromSet()이 둘 다 VISCA 코드만
  // 보게 되어, 캐시의 wbMode와 확인 조회(`09 04 35`)가 자동으로 맞아떨어진다.
  if (data1 == PELCO_EDIS_SET_WB_MODE) data1 = VISCA_CAM_WB_MODE;

  // One Push AF만은 pp를 그대로 흘리지 않고 `04 38 04`(Focus AF 모드 = One Push)로
  // 바꿔 보낸다. AWB와 달리 pp(0x18)는 진짜 VISCA 코드가 맞는데, FoMaKo가 그 코드를
  // 구현하지 않아서 다른 코드로 우회하는 경우다.
  //
  // 실측 근거 (2026-08-08, doc/todo.md 1.1절): `04 18 01`(표준 Sony VISCA의 CAM_Focus
  // One Push Trigger)을 그대로 넘기면 FoMaKo가 초점을 잡지 않고 `E0 60 02 FF`를
  // 돌려준다. 에러 코드 0x02는 Syntax Error - "지금은 실행할 수 없다"(0x41)가 아니라
  // 명령 자체를 모른다는 뜻이므로, FoMaKo는 `04 18`을 구현하지 않았다. 반면 `04 38 04`는
  // `E0 41 FF`/`E0 51 FF`(ACK/Completion)를 받고 실제로 초점을 잡는다.
  //
  // 다만 모드를 One Push로 바꾼 채로 두면 안 된다 - 카메라가 AF 모드에 머물러 이후
  // 수동 초점 조작(Focus Near/Far)을 거부한다(실측 2026-08-08). 컨트롤러 입장에서
  // One Push AF는 Manual 상태에서 누르는 일회성 트리거일 뿐이므로, 초점을 잡을 여유를
  // 준 뒤 Manual로 되돌려야 조작 모델이 맞는다 - pollOnePushAfRestore()가 담당한다.
  if (data1 == PELCO_EDIS_SET_FOCUS_TRIGGER && data2 == 0x01) {
    uint8_t buf[6] = {0, 0x01, 0x04, VISCA_CAM_FOCUS_AF_MODE, VISCA_FOCUS_MODE_ONE_PUSH,
                      VISCA_TERMINATOR};
    forwardTranslatedVisca(camNumber, buf, sizeof(buf), debugTag);
    updateModeCacheFromSet(camNumber, VISCA_CAM_FOCUS_AF_MODE, VISCA_FOCUS_MODE_ONE_PUSH);
    onePushRestoreCam = camNumber;
    onePushRestoreDueMs = millis() + VISCA_ONE_PUSH_AF_SETTLE_MS;
    return;
  }

  // 컨트롤러가 Focus 모드를 직접 지정하면(Auto/Manual 키) 위에서 걸어둔 되돌리기 예약을
  // 취소한다 - 안 그러면 잠시 뒤 예약이 깨어나 조작자가 방금 고른 모드를 덮어쓴다.
  if (data1 == VISCA_CAM_FOCUS_AF_MODE && onePushRestoreCam == camNumber) {
    onePushRestoreCam = 0;
  }

  // 상대 조정은 같은 바이트의 반복이 곧 "한 칸 더"라서 중복 억제를 건너뛴다. 나머지
  // 파라미터는 절대 모드(멱등)라 억제해도 결과가 같으므로 그대로 둔다.
  bool isRelativeStep = (data1 == VISCA_CAM_RGAIN || data1 == VISCA_CAM_BGAIN ||
                         data1 == VISCA_CAM_SHUTTER || data1 == VISCA_CAM_EXP_COMP);

  uint8_t buf[6] = {0, 0x01, 0x04, data1, data2, VISCA_TERMINATOR};
  forwardTranslatedVisca(camNumber, buf, sizeof(buf), debugTag, isRelativeStep);
  updateModeCacheFromSet(camNumber, data1, data2);
}

// Pelco-D/P Standard/Extended Command를 VISCA 명령으로 변환해서 보낸다.
// isPelcoP로 CMND1 비트 배치 차이(Focus Near/Far 위치, doc/pelcoP_command.md 4절)만
// 분기하고, 나머지(Pan/Tilt/Zoom 비트, Preset/Query 옵코드)는 두 프로토콜이 동일한
// 값 체계를 쓰므로 공유한다 (doc/pelcoP_command.md 5절 "CMND2 값이 Pelco-D 표와
// 완전히 동일" 참고). doc/pelcoD_command.md 7.1절에서 FoMaKo 자체 지원이 확인된
// 범위만 구현한다 - Run Group/Swing, Aux, 절대좌표 Set, Focus Position Query는
// FoMaKo Pelco-D/P 표 자체에 없어서 애초에 구현 대상이 아니다 (같은 문서 10절).
//
// 호출부는 반환값으로 합성 ACK(General Response)를 보낼지 정한다 - 무시한 명령에 ACK를
// 돌려주면 "받아서 처리했다"는 거짓 신호가 되고, 계속 들어오는 폴링 명령마다 RS485를
// 4바이트씩 점유하며 그동안 수신도 막힌다.
bool translatePelcoAndForward(uint8_t camNumber, uint8_t cmnd1, uint8_t cmnd2, uint8_t data1,
                               uint8_t data2, bool isPelcoP, const uint8_t* rawPacket,
                               uint8_t rawLen, const char* debugTag) {
  // EDIS 벤더 확장 (config.h의 PELCO_EDIS_* 참고). Pelco-D에서만 실측했으므로
  // Pelco-P 입력에는 적용하지 않는다.
  if (!isPelcoP && cmnd1 == 0x00 &&
      (cmnd2 == PELCO_EDIS_SET_CMD || cmnd2 == PELCO_EDIS_QUERY_CMD)) {
    handleEdisVendorCommand(camNumber, cmnd2, data1, data2, rawPacket, rawLen, debugTag);
    // SET에도 GET에도 General Response는 보내지 않는다 - 실물 ED-P 카메라가 SET에는
    // 아무 응답도 하지 않고, GET에는 전용 D7 응답만 돌려주는 게 실측으로 확인됐다.
    return false;
  }

  // Extended Command(Preset/Query)는 CMND1=0x00 + CMND2가 아래 고정 옵코드값(전부
  // 홀수)일 때만 성립한다. Standard Command 비트 플래그는 CMND2 bit0이 항상 0으로
  // 정의되어 있어(4절) 짝수이므로, 홀수 옵코드와 절대 겹치지 않는다.
  if (cmnd1 == 0x00) {
    if (cmnd2 == 0x03 || cmnd2 == 0x05 || cmnd2 == 0x07) {
      uint8_t opcode = (cmnd2 == 0x03) ? 0x01 : (cmnd2 == 0x07) ? 0x02 : 0x00;
      uint8_t buf[7] = {0, 0x01, 0x04, 0x3F, opcode, data2, VISCA_TERMINATOR};
      forwardTranslatedVisca(camNumber, buf, sizeof(buf), debugTag);
      return true;
    }
    if (cmnd2 == 0x51 || cmnd2 == 0x53 || cmnd2 == 0x55) {
      // Query Pan/Tilt/Zoom Position: VISCA 조회는 비동기 응답(별도 UDP 패킷)이 와야
      // 완성되는데, 그걸 Pelco Extended Response로 재포장하는 로직은 아직 없다.
      // 요청만 받고 조용히 무시한다 (향후 작업, doc/pelcoD_command.md 10절 참고).
      // 애초에 Query가 기대하는 건 값이 실린 Extended Response지 General Response(ACK)가
      // 아니므로, 여기서 ACK를 돌려주는 것도 맞지 않는다 - false를 반환한다.
      char reason[40];
      snprintf(reason, sizeof(reason), "Query Position not implemented (CMND2=0x%02X)", cmnd2);
      diagnostics.recordUnhandledPacket(camNumber, reason, rawPacket, rawLen);
      return false;
    }
  }

  // 여기까지 왔는데 CMND2 bit0이 켜져 있으면, 우리가 해석할 줄 모르는 Extended
  // Command다. Standard Command의 비트 플래그는 CMND2 bit0이 항상 0으로 정의되어
  // 있으므로(4절), 이걸 아래 모션 비트 디코드로 흘려보내면 옵코드 값이 통째로
  // Pan/Tilt/Zoom/Focus 비트로 오독된다.
  //
  // 실측 사례: ZU-EPC7000이 아이들 상태에서도 계속 보내는 폴링 명령이 CMND2=0xD3인데,
  // 이게 Pan Right(0x02) + Tilt Down(0x10) + Zoom Wide(0x40) + Focus Far(0x80)로
  // 해석되어 컨트롤러를 건드리지 않아도 카메라가 오른쪽 아래로 계속 밀렸다. 0xD3은
  // 표준 Extended 옵코드 표(0x03~0x6F) 밖의 EDIS ED-P 벤더 고유 명령으로 보이며,
  // 의미를 모르는 확장 명령은 모션으로 오역하느니 무시하는 게 맞다.
  if (cmnd2 & 0x01) {
    if (routingTable.get().debugMode) {
      Serial.print("[");
      Serial.print(debugTag);
      Serial.print("] Unknown extended command CMND2=0x");
      Serial.print(cmnd2, HEX);
      Serial.println(" - ignored");
    }
    {
      char reason[40];
      snprintf(reason, sizeof(reason), "Unknown extended command CMND2=0x%02X", cmnd2);
      diagnostics.recordUnhandledPacket(camNumber, reason, rawPacket, rawLen);
    }
    return false;
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
    return true;
  }

  bool up = cmnd2 & 0x08, down = cmnd2 & 0x10, left = cmnd2 & 0x04, right = cmnd2 & 0x02;
  bool zoomTele = cmnd2 & 0x20, zoomWide = cmnd2 & 0x40;
  // CMND1은 두 프로토콜의 비트 배치가 한 자리씩 밀려 있다 - Pelco-D는 bit7을 Sense로
  // 쓰지만 Pelco-P는 그 자리가 없고 Focus Far가 CMND2에서 CMND1으로 옮겨왔다
  // (doc/pelcoP_command.md 4절). Focus뿐 아니라 Iris 자리도 같이 밀리므로 함께 분기한다.
  bool focusNear, focusFar, irisOpen, irisClose;
  if (isPelcoP) {
    focusFar = cmnd1 & 0x01;
    focusNear = cmnd1 & 0x02;
    irisOpen = cmnd1 & 0x04;
    irisClose = cmnd1 & 0x08;
  } else {
    focusNear = cmnd1 & 0x01;
    focusFar = cmnd2 & 0x80;
    irisOpen = cmnd1 & 0x02;
    irisClose = cmnd1 & 0x04;
  }

  // 아래 세 블록 중 하나라도 실제로 명령을 내보냈는지 - 비트가 하나도 안 켜진
  // (Stop도 아닌) 패킷이면 아무것도 안 하고 false로 빠져나간다.
  bool acted = false;

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
    acted = true;
  }

  if (zoomTele || zoomWide) {
    uint8_t buf[6] = {0, 0x01, 0x04, 0x07, (uint8_t)(zoomTele ? 0x02 : 0x03), VISCA_TERMINATOR};
    forwardTranslatedVisca(camNumber, buf, sizeof(buf), debugTag);
    acted = true;
  }

  if (focusNear || focusFar) {
    uint8_t buf[6] = {0, 0x01, 0x04, 0x08, (uint8_t)(focusFar ? 0x02 : 0x03), VISCA_TERMINATOR};
    forwardTranslatedVisca(camNumber, buf, sizeof(buf), debugTag);
    acted = true;
  }

  // Iris Open/Close -> FoMaKo 매뉴얼 VISCA 표의 CAM_Iris Up/Down(`04 0B 02`/`04 0B 03`).
  // Up이 조리개를 여는(밝아지는) 쪽이다. 한 번에 한 칸씩 움직이는 계단식 명령이라,
  // 컨트롤러가 버튼을 누르고 있는 동안 반복해 보내는 패킷이 그대로 한 칸씩 쌓인다.
  //
  // ZU-EPC7000의 28행 커맨드 표(doc/pelcoD_command.md 5절)에도, FoMaKo 자체 Pelco-D
  // 표(7.1절)에도 Iris 행이 없어서 처음엔 구현 대상에서 빠져 있었다. 그런데 컨트롤러가
  // 실제로는 `FF 06 02 00 00 00 08`(Pelco-D CMND1 bit1)을 보내는 게 실측으로 확인돼
  // (2026-08-08) 추가했다 - 표준 Pelco-D 비트 배치 자체에는 원래 있던 자리다.
  //
  // AE 모드가 Manual이 아니면 카메라가 조리개를 도로 가져가므로 효과가 없다. 컨트롤러에
  // Iris Auto/Manual 키가 따로 있어(`C3 39 00`/`C3 39 03`) 조작자가 직접 고르는 값이므로,
  // 여기서 모드를 대신 바꾸지는 않는다.
  if (irisOpen || irisClose) {
    uint8_t buf[6] = {0, 0x01, 0x04, 0x0B, (uint8_t)(irisOpen ? 0x02 : 0x03), VISCA_TERMINATOR};
    forwardTranslatedVisca(camNumber, buf, sizeof(buf), debugTag);
    acted = true;
  }

  // 여기까지 와서 아무것도 못 보냈다면, 비트는 켜져 있는데(Stop은 위에서 이미 걸러졌다)
  // 우리가 번역할 줄 모르는 자리다 - Camera On/Off, Auto/Manual Scan 같은 것들. 조용히
  // 버리면 Iris 때처럼 "명령은 나가는데 카메라가 안 움직인다"는 증상만 남고 원인을
  // 짚을 단서가 없으므로, Unhandled Commands 로그에 남긴다.
  if (!acted) {
    char reason[48];
    snprintf(reason, sizeof(reason), "No translatable bit (CMND1=0x%02X CMND2=0x%02X)", cmnd1,
             cmnd2);
    diagnostics.recordUnhandledPacket(camNumber, reason, rawPacket, rawLen);
  }

  return acted;
}

// 이 게이트웨이가 "담당하는" 슬롯인지 - 즉 카메라 슬롯 1~7 안이면서 IP까지 설정된
// 슬롯인지 판정한다. 합성 ACK를 보낼지 말지의 기준이다.
//
// RS485는 2선 멀티드롭이라 컨트롤러가 직접 제어하는 실물 Pelco 카메라(EDIS ED-P 등)가
// 같은 버스에 함께 물려 있을 수 있고, 그 카메라들은 자기 앞으로 온 명령에 스스로
// 응답한다. 게이트웨이가 주소를 가리지 않고 ACK를 쏘면 드라이버 두 개가 동시에 버스를
// 물어(bus contention) 컨트롤러가 양쪽 응답을 다 못 읽는다. IP가 설정된 슬롯 = 실물
// 카메라가 아니라 이 게이트웨이가 IP로 중계하는 대상이므로, 그 주소에만 응답한다.
bool isOwnedSlot(const CameraSlot* slot) {
  return slot != nullptr && slot->isConfigured();
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
  recordAutoPowerChatterPacket();

  SystemConfig& cfg = routingTable.get();
  if (cfg.debugMode) {
    Serial.print("[PELCO-D RX] ");
    Serial.println(viscaBytesToHex(data, len));
  }

  // Pelco-D ADDR은 실제 주소를 그대로 쓴다 (doc/pelcoD_command.md 2절) - 카메라
  // 슬롯 1~7 밖(8 이상, 0)은 이 프로젝트의 매핑 대상이 아니라 무시한다 (8.2절).
  uint8_t camNumber = data[1];
  CameraSlot* slot = routingTable.camera(camNumber);  // 1~7 밖이면 nullptr

  if (slot == nullptr) {
    if (cfg.debugMode) {
      Serial.println("[ACTION] Address out of range (1-7) - ignored");
    }
    return;
  }

  bool needsAck = translatePelcoAndForward(camNumber, data[2], data[3], data[4], data[5],
                                          /*isPelcoP=*/false, data, len, "PELCO-D");

  // ACK는 번역/전달이 실제로 일어난 뒤에, 실제로 처리한 명령에 대해서만 보낸다.
  if (cfg.pelcoResponseMode == PelcoResponseMode::SYNTHETIC && isOwnedSlot(slot) && needsAck) {
    sendPelcoDResponse(data, len);
    if (cfg.debugMode) {
      Serial.println("[TX] Pelco-D General Response (ACK)");
    }
  }
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
  recordAutoPowerChatterPacket();

  SystemConfig& cfg = routingTable.get();
  if (cfg.debugMode) {
    Serial.print("[PELCO-P RX] ");
    Serial.println(viscaBytesToHex(data, len));
  }

  // Pelco-P ADDR은 "실제 주소 - 1"을 wire에 싣는다 (doc/pelcoP_command.md 1/7절,
  // FUJIFILM SX1600 스펙 "ONE MINUS THE ADDRESS SET BY THE DEVICE") - Pelco-D와
  // 달리 +1 보정이 필요하다. data[1]==254/255처럼 비정상적으로 큰 값이 와도
  // camNumber가 8 이상(또는 0, uint8_t 오버플로우 시)이 되어 아래 범위 검사에서
  // 자연스럽게 걸러진다.
  uint8_t camNumber = data[1] + 1;
  CameraSlot* slot = routingTable.camera(camNumber);  // 1~7 밖이면 nullptr

  if (slot == nullptr) {
    if (cfg.debugMode) {
      Serial.println("[ACTION] Address out of range (1-7) - ignored");
    }
    return;
  }

  bool needsAck = translatePelcoAndForward(camNumber, data[2], data[3], data[4], data[5],
                                          /*isPelcoP=*/true, data, len, "PELCO-P");

  // ACK는 번역/전달이 실제로 일어난 뒤에, 실제로 처리한 명령에 대해서만 보낸다.
  if (cfg.pelcoResponseMode == PelcoResponseMode::SYNTHETIC && isOwnedSlot(slot) && needsAck) {
    sendPelcoPResponse(data, len);
    if (cfg.debugMode) {
      Serial.println("[TX] Pelco-P General Response (ACK)");
    }
  }
}

bool isPelcoInput(const SystemConfig& cfg) {
  return cfg.inputProtocol == InputProtocol::PELCO_D ||
         cfg.inputProtocol == InputProtocol::PELCO_P ||
         cfg.inputProtocol == InputProtocol::PELCO_AUTO;
}

// (카메라, 항목) 하나를 VISCA로 조회한다. 보냈으면 true.
bool sendModeInquiry(uint8_t camNumber, uint8_t item, const SystemConfig& cfg) {
  CameraSlot* slot = routingTable.camera(camNumber);
  if (slot == nullptr || !slot->isConfigured()) return false;

  // ModeInquiry의 열거 순서(POWER, AE, WB, FOCUS, BACKLIGHT)와 같은 순서여야 한다.
  // 전부 카메라로 나가는 VISCA 코드다 - WB는 컨트롤러가 쓰는 0x36이 아니라 0x35다.
  static const uint8_t kInquiryCodes[MODE_INQUIRY_ITEM_COUNT] = {
      VISCA_CAM_POWER, 0x39, VISCA_CAM_WB_MODE, VISCA_CAM_FOCUS_AF_MODE, VISCA_CAM_BACKLIGHT};
  uint8_t buf[5] = {(uint8_t)(VISCA_ADDR_CAM1 + camNumber - 1), 0x09, 0x04,
                    kInquiryCodes[item], VISCA_TERMINATOR};

  // 명령과 같은 경로(routingTable.route)를 태워야 슬롯의 Address Mode가 조회에도
  // 똑같이 적용된다. 직접 보내면 Address Mode가 rewrite_0x81인 슬롯에서 명령은
  // 0x81로, 조회는 0x86으로 나가 카메라가 조회만 무시하는 상황이 생긴다.
  RoutedPacket routed[1];
  if (routingTable.route(buf, sizeof(buf), routed, 1) == 0) return false;
  if (!sendToCamera(*routed[0].slot, routed[0].output, routed[0].outputLen)) return false;

  if (cfg.debugMode) {
    Serial.print("[MODE] CAM");
    Serial.print(camNumber);
    Serial.print(" -> ");
    Serial.println(viscaBytesToHex(routed[0].output, routed[0].outputLen));
  }

  pendingInquiry = (ModeInquiry)(item + 1);  // NONE 다음이 POWER
  pendingInquiryCam = camNumber;
  lastInquiryMs = millis();
  return true;
}

// 카메라의 전원/AE/WB/Focus 모드를 VISCA로 하나씩 조회해 modeCache를 채운다. 네 조회의
// 응답이 전부 `y0 50 pp FF`로 똑같이 생겨서 동시에 던지면 구분할 수 없으므로, 답을
// 받거나 타임아웃될 때까지 다음 조회를 보내지 않는다. 설정된 슬롯들을 (카메라, 항목)
// 쌍으로 라운드로빈하되, C3 SET 직후에는 그 항목을 먼저 확인한다.
// pendingInquiry가 MODE_INQUIRY_TIMEOUT_MS 안에 응답을 못 받으면 여기서 놓아준다. Pelco
// 라운드로빈(pollCameraModeInquiries)이든 Auto Power Control(reconcileAutoPower)이든
// pendingInquiry 슬롯 하나를 공유해서 조회를 보내므로, 어느 쪽이 보냈든 이 타임아웃 처리는
// 입력 프로토콜과 무관하게(Pelco 게이팅 없이) 항상 돌아야 한다 - pollCameraModeInquiries
// 안에만 있으면 VISCA 입력(isPelcoInput()==false)에서 Auto Power Control이 보낸 조회가
// 응답 없이 pendingInquiry를 영영 붙들어 이후 모든 조회(라운드로빈이든 재시도든)가 막힌다.
void pollInquiryTimeout() {
  if (pendingInquiry == ModeInquiry::NONE) return;
  if (millis() - lastInquiryMs < MODE_INQUIRY_TIMEOUT_MS) return;

  // 응답 없음 - 이 항목은 이번 회차를 포기한다. 카메라가 조회를 지원하지 않아도 여기서
  // 자연스럽게 흘러가고, 캐시는 마지막으로 알던 값을 유지한다. 원인을 눈으로 볼 수 있도록
  // 타임아웃을 로그로 남긴다.
  if (routingTable.get().debugMode) {
    Serial.print("[MODE] CAM");
    Serial.print(pendingInquiryCam);
    Serial.println(" inquiry timed out - no reply");
  }
  pendingInquiry = ModeInquiry::NONE;
}

// One Push AF 트리거(`04 38 04`)로 올려둔 Focus 모드를 Manual(`04 38 03`)로 되돌린다.
//
// 트리거 직후에 바로 되돌리면 초점을 잡는 도중에 끊기므로 VISCA_ONE_PUSH_AF_SETTLE_MS를
// 기다렸다가 보낸다. 되돌리지 않고 One Push 모드로 두면 카메라가 이후 Focus Near/Far를
// 거부해서, 컨트롤러 LCD에는 Manual이라고 떠 있는데 실제로는 초점이 안 움직이는
// 상태가 된다(실측 2026-08-08, doc/todo.md 1.1절).
//
// Manual로 고정해 되돌리는 게 맞다 - 컨트롤러의 ONE PUSH AF 키는 Manual Focus 상태에서만
// 누르는 트리거 버튼이라(1.1절), 트리거 전 모드가 Manual이 아닌 경우가 애초에 없다.
void pollOnePushAfRestore() {
  if (onePushRestoreCam == 0) return;
  if ((long)(millis() - onePushRestoreDueMs) < 0) return;

  uint8_t cam = onePushRestoreCam;
  onePushRestoreCam = 0;

  uint8_t buf[6] = {0, 0x01, 0x04, VISCA_CAM_FOCUS_AF_MODE, VISCA_FOCUS_MODE_MANUAL,
                    VISCA_TERMINATOR};
  forwardTranslatedVisca(cam, buf, sizeof(buf), "ONE-PUSH AF");
  updateModeCacheFromSet(cam, VISCA_CAM_FOCUS_AF_MODE, VISCA_FOCUS_MODE_MANUAL);
}

void pollCameraModeInquiries() {
  SystemConfig& cfg = routingTable.get();
  if (!isPelcoInput(cfg) || WiFi.status() != WL_CONNECTED) return;
  if (pendingInquiry != ModeInquiry::NONE) return;  // 타임아웃 여부는 pollInquiryTimeout()이 처리

  unsigned long now = millis();

  // C3 SET 직후 예약된 확인 조회는 라운드로빈 순서를 건너뛰고 먼저 나간다.
  bool priorityDue = (priorityInquiryCam != 0 && (long)(now - priorityInquiryDueMs) >= 0);
  if (!priorityDue && now - lastInquiryMs < MODE_INQUIRY_INTERVAL_MS) return;

  if (priorityDue) {
    uint8_t cam = priorityInquiryCam;
    uint8_t item = priorityInquiryItem;
    priorityInquiryCam = 0;
    if (sendModeInquiry(cam, item, cfg)) return;
    // 못 보냈으면(슬롯 미설정 등) 그냥 아래 라운드로빈으로 넘어간다.
  }

  // 다음 (카메라, 항목) 쌍으로 커서를 옮긴다. 항목을 다 돌면 다음 카메라로.
  for (uint8_t tries = 0; tries < CAMERA_SLOT_COUNT * MODE_INQUIRY_ITEM_COUNT; tries++) {
    inquiryItemCursor++;
    if (inquiryItemCursor >= MODE_INQUIRY_ITEM_COUNT) {
      inquiryItemCursor = 0;
      inquiryCamCursor = (inquiryCamCursor % CAMERA_SLOT_COUNT) + 1;
    }
    if (sendModeInquiry(inquiryCamCursor, inquiryItemCursor, cfg)) return;
  }
}

// 카메라 응답이 우리가 던진 모드 조회의 답이면 캐시를 갱신하고 true를 반환한다.
// VISCA 조회 응답은 `y0 50 pp FF` 4바이트다 - ACK(`y0 41 FF`)나 Completion
// (`y0 51 FF`)은 3바이트라 길이와 두 번째 바이트로 구분된다.
bool consumeModeInquiryReply(uint8_t camNumber, const uint8_t* buf, uint8_t len) {
  if (pendingInquiry == ModeInquiry::NONE || pendingInquiryCam != camNumber) return false;
  if (len != 4 || buf[1] != 0x50 || buf[3] != VISCA_TERMINATOR) return false;

  CameraModeCache& c = modeCache[camNumber - 1];
  switch (pendingInquiry) {
    case ModeInquiry::POWER: {
      c.power = buf[2];
      // Auto Power Control의 reconcileAutoPower()가 던진 확인 조회의 답일 수도 있다 -
      // 그 경우 실측 상태로 confirmed를 갱신한다. auto power control이 꺼진 카메라에도
      // 그냥 채워두지만, 그 카메라의 target은 항상 UNKNOWN이라 reconcile은 손대지 않는다.
      uint8_t i = camNumber - 1;
      if (buf[2] == 0x02) autoPowerConfirmed[i] = AutoPowerState::ON;
      else if (buf[2] == 0x03) autoPowerConfirmed[i] = AutoPowerState::OFF;
      // 목표에 도달했으면 재시도 backoff를 기본값으로 되돌려, 나중에 다시 어긋났을 때
      // 처음(10초)부터 재시도하게 한다.
      if (autoPowerConfirmed[i] == autoPowerTarget[i]) {
        autoPowerRetryDelayMs[i] = AUTO_POWER_VERIFY_DELAY_MS;
      }
      break;
    }
    case ModeInquiry::AE: c.aeMode = buf[2]; break;
    case ModeInquiry::WB: c.wbMode = buf[2]; break;
    case ModeInquiry::FOCUS: c.focusMode = buf[2]; break;
    case ModeInquiry::BACKLIGHT: c.backlight = buf[2]; break;
    default: break;
  }
  pendingInquiry = ModeInquiry::NONE;
  return true;
}

// camNumber에 VISCA CAM_Power On/Standby 명령을 보낸다. sendModeInquiry()와 같은 이유로
// routingTable.route()를 태운다 - 슬롯의 Address Mode가 명령에도 조회와 동일하게 적용돼야
// 한다.
bool sendAutoPowerCommand(uint8_t camNumber, bool on, const SystemConfig& cfg) {
  uint8_t buf[6] = {(uint8_t)(VISCA_ADDR_CAM1 + camNumber - 1), 0x01, 0x04, 0x00,
                    (uint8_t)(on ? 0x02 : 0x03), VISCA_TERMINATOR};

  RoutedPacket routed[1];
  if (routingTable.route(buf, sizeof(buf), routed, 1) == 0) return false;
  if (!sendToCamera(*routed[0].slot, routed[0].output, routed[0].outputLen)) return false;

  if (cfg.debugMode) {
    Serial.print("[AUTO-POWER] CAM");
    Serial.print(camNumber);
    Serial.print(on ? " -> On  " : " -> Standby  ");
    Serial.println(viscaBytesToHex(routed[0].output, routed[0].outputLen));
  }
  return true;
}

// 텀블링 10초 윈도우가 닫힐 때마다 그 안에 관측된 유효 패킷 수로 auto power control이
// 켜진 카메라들의 목표(target) 상태를 정한다 (config.h AUTO_POWER_* 참고). 실제 VISCA
// 명령은 여기서 보내지 않는다 - 목표만 바꿔두고, reconcileAutoPower()가 명령 전송/확인/
// 재시도를 전담한다. 목표가 실제로 바뀔 때만(edge-triggered) 재시도 backoff를 초기화하고
// 즉시 시도하게 예약한다 - 조건이 계속 유지되는 동안 매 윈도우마다 같은 명령을 다시
// 만들어내지 않기 위함이다.
void updateAutoPowerFromChatter() {
  SystemConfig& cfg = routingTable.get();
  if (cfg.inputProtocol == InputProtocol::RAW_BRIDGE) return;

  unsigned long now = millis();
  if (now - autoPowerWindowStartMs < AUTO_POWER_CHATTER_WINDOW_MS) return;

  uint16_t count = autoPowerChatterCount;
  autoPowerChatterCount = 0;
  autoPowerWindowStartMs = now;

  for (uint8_t camNumber = 1; camNumber <= CAMERA_SLOT_COUNT; camNumber++) {
    CameraSlot* slot = routingTable.camera(camNumber);
    if (slot == nullptr || !slot->isConfigured() || !slot->autoPowerControl) continue;

    uint8_t i = camNumber - 1;
    AutoPowerState desired = autoPowerTarget[i];
    if (count >= AUTO_POWER_ON_THRESHOLD) desired = AutoPowerState::ON;
    else if (count == 0) desired = AutoPowerState::OFF;
    // count가 1~2개면 desired를 안 바꾼다 - 켤지 끌지 판단하기엔 근거가 애매한 경계
    // 구간이라, 위에서 desired를 현재 target으로 초기화해뒀으므로 자연히 유지된다.

    if (desired != autoPowerTarget[i]) {
      autoPowerTarget[i] = desired;
      autoPowerAwaitingVerify[i] = false;                      // 새 목표 - 명령부터 다시 보낸다
      autoPowerRetryDelayMs[i] = AUTO_POWER_VERIFY_DELAY_MS;    // backoff 초기화
      autoPowerActionDueMs[i] = now;                            // 다음 reconcile tick에 바로 시도
    }
  }
}

// target(목표)과 confirmed(실측 확인) 상태가 다른 카메라에 대해 VISCA CAM_Power 명령을
// 보내고, AUTO_POWER_VERIFY_DELAY_MS 뒤 CAM_PowerInq로 실제 반영됐는지 확인한다. 확인
// 결과가 기대와 다르거나(카메라가 명령을 거부) 응답 자체가 없으면(카메라 연결 끊김 등)
// 명령을 다시 보낸다 - 재시도 간격은 실패할 때마다 두 배로 늘어 AUTO_POWER_RETRY_MAX_DELAY_MS
// (5분)에서 멈춘다. 카메라가 계속 응답하지 않아도 이 상한 안에서 낮은 빈도로 계속
// 재시도하며(WiFi 재연결과 같은 패턴 - kWifiRetryIntervalMs 참고), 다시 응답하기 시작하면
// 자동으로 복구된다.
//
// sendModeInquiry()와 응답 형식이 같은 pendingInquiry 한 슬롯을 공유하므로, 이미 다른
// 조회가 진행 중이면 이번 tick은 건너뛴다.
void reconcileAutoPower() {
  SystemConfig& cfg = routingTable.get();
  if (cfg.inputProtocol == InputProtocol::RAW_BRIDGE || WiFi.status() != WL_CONNECTED) return;

  unsigned long now = millis();

  for (uint8_t camNumber = 1; camNumber <= CAMERA_SLOT_COUNT; camNumber++) {
    CameraSlot* slot = routingTable.camera(camNumber);
    if (slot == nullptr || !slot->isConfigured() || !slot->autoPowerControl) continue;

    uint8_t i = camNumber - 1;
    if (autoPowerTarget[i] == AutoPowerState::UNKNOWN) continue;    // 아직 청취 판정 전
    if (autoPowerConfirmed[i] == autoPowerTarget[i]) continue;      // 이미 목표대로 확인됨
    if ((long)(now - autoPowerActionDueMs[i]) < 0) continue;        // 아직 때가 안 됨

    if (!autoPowerAwaitingVerify[i]) {
      // 명령을 보낸다 - 실패해도(네트워크 순간 문제 등) 다음 재시도 tick에 다시 시도된다.
      sendAutoPowerCommand(camNumber, autoPowerTarget[i] == AutoPowerState::ON, cfg);
      autoPowerAwaitingVerify[i] = true;
      autoPowerActionDueMs[i] = now + AUTO_POWER_VERIFY_DELAY_MS;
    } else {
      // 반영됐는지 확인할 시간 - 조회를 보낸다. 응답은 비동기로 consumeModeInquiryReply()가
      // confirmed를 갱신하므로, 여기서는 이번 시도가 실패했다고 미리 가정하고 다음 재시도
      // (명령 재전송)를 예약해둔다 - 응답이 실제로 목표와 맞게 오면 위 두 번째 continue
      // 조건(confirmed==target)에서 다음 tick에 곧바로 걸러져 재전송이 취소된다.
      if (pendingInquiry != ModeInquiry::NONE) continue;  // 다른 조회가 진행 중 - 다음 tick에

      sendModeInquiry(camNumber, /*item=POWER*/ 0, cfg);
      autoPowerAwaitingVerify[i] = false;  // 다음엔 다시 명령 전송부터 (실패를 기본 가정)
      autoPowerActionDueMs[i] = now + autoPowerRetryDelayMs[i];
      unsigned long doubled = autoPowerRetryDelayMs[i] * 2;
      autoPowerRetryDelayMs[i] = (doubled > AUTO_POWER_RETRY_MAX_DELAY_MS || doubled < autoPowerRetryDelayMs[i])
                                     ? (unsigned long)AUTO_POWER_RETRY_MAX_DELAY_MS
                                     : doubled;
    }
  }
}

// 카메라로부터의 응답을 non-blocking으로 확인한다. 모드 조회 답변은 캐시로 흡수하고,
// 나머지는 Response Mode가 forward/forward_rewrite일 때 RS485로 전달한다.
void pollCameraResponses() {
  SystemConfig& cfg = routingTable.get();
  bool pelcoInput = isPelcoInput(cfg);
  bool wantForward = (cfg.responseMode == ResponseMode::FORWARD ||
                      cfg.responseMode == ResponseMode::FORWARD_REWRITE);

  // Pelco 입력에서는 D3 19 상태 응답을 만들기 위해 모드 조회 답변을 반드시 읽어야
  // 하므로, Response Mode와 무관하게 UDP를 확인한다.
  if (!wantForward && !pelcoInput) return;

  uint8_t buf[VISCA_BUFFER_SIZE];
  IPAddress remoteIp;

  uint8_t len = ipViscaClient.receive(buf, sizeof(buf), &remoteIp);
  if (len == 0) len = sonyViscaClient.receive(buf, sizeof(buf), &remoteIp);
  if (len == 0) return;

  for (uint8_t camNumber = 1; camNumber <= CAMERA_SLOT_COUNT; camNumber++) {
    CameraSlot* slot = routingTable.camera(camNumber);
    if (!slot->isConfigured() || slot->ip.toIPAddress() != remoteIp) continue;

    if (consumeModeInquiryReply(camNumber, buf, len)) {
      if (cfg.debugMode) {
        Serial.print("[MODE] CAM");
        Serial.print(camNumber);
        Serial.print(" <- ");
        Serial.println(viscaBytesToHex(buf, len));
      }
      return;
    }

    // 입력이 Pelco 계열이면 raw VISCA 바이트를 RS485로 내보내지 않는다. 두 가지 이유다.
    //
    // 1) 컨트롤러가 해석하지 못한다. Pelco 컨트롤러는 Pelco 응답 포맷을 기대하는데
    //    VISCA ACK/Completion(`z0 41 FF`/`z0 51 FF`)은 전혀 다른 체계다.
    // 2) 더 나쁜 건 버스 오염이다. VISCA 응답의 종료 바이트 0xFF가 Pelco-D의 SYNC
    //    바이트와 같아서, 같은 RS485 버스에 물린 다른 Pelco 장비(컨트롤러가 직접
    //    제어하는 실물 카메라)가 그 자리에서 새 프레임을 시작해버린다. 그러면 뒤이어
    //    오는 진짜 명령의 앞부분을 그 유령 프레임이 삼켜서 통째로 깨진다.
    //
    // 어차피 Pelco 쪽 ACK은 sendPelcoDResponse()/sendPelcoPResponse()가 이미 즉시
    // 합성해서 돌려주고 있어, VISCA ACK/Completion을 중계해봐야 컨트롤러에 새로 줄
    // 정보가 없다. 값이 실린 조회 응답(위치 질의 등)을 Pelco Extended Response로
    // 재포장하는 건 별도 작업이다 (doc/pelcoD_command.md 10절).
    //
    // 진단 목적은 유지한다 - Debug Mode에서는 받은 바이트를 그대로 보여준다.
    if (pelcoInput) {
      if (cfg.debugMode) {
        Serial.print("Camera response from ");
        Serial.print(remoteIp);
        Serial.print(": ");
        Serial.print(viscaBytesToHex(buf, len));
        Serial.println("  (not forwarded - raw VISCA would corrupt the Pelco bus)");
      }
      return;
    }

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
    // UART0: USB Serial 메뉴/디버그 전용. RS485 쪽 속도(보통 9600)와 무관하게 최대한
    // 빠르게 잡는다 - Debug Mode에서 패킷당 170자 가까이 찍는데, 9600bps면 그것만으로
    // 약 177ms가 걸리고 Serial.print()는 TX 버퍼가 차면 블로킹하므로 그동안 loop()가
    // 멈춰 RS485 수신 바이트를 놓친다. Stop 명령이 유실되면 카메라가 안 멈춘다.
    // 115200이면 같은 출력이 약 15ms로 줄어든다.
    Serial.begin(SERIAL_CONSOLE_BAUD);
    delay(200);
    // 부팅 배너를 일부러 찍지 않는다 - 리셋 직후 Serial 메뉴가 잠금 해제(Enter 두 번)
    // 되기 전까지는 어떤 메시지도 안 보내는 게 의도다. connectWifi()/maintainWifi()도
    // 같은 이유로 메시지를 serialMenu.menuActive()로 게이팅한다.
  }

  diagnostics.begin();
  statusLed.begin(cfg.statusLedPin);
  resetModeCache();
  resetAutoPowerControl();
  // 부팅 직후 아직 아무 트래픽도 못 봤는데 millis()가 이미 10초를 넘긴 걸로 오판해
  // 곧장 Standby부터 내려버리는 걸 막는다 - 첫 윈도우도 정상적으로 10초를 채우게 한다.
  autoPowerWindowStartMs = millis();

  rs485.begin(cfg.rs485Baudrate, cfg.rs485RxPin, cfg.rs485TxPin, cfg.rs485DeRePin,
              cfg.rs485Uart0Shared, cfg.rs485Invert);
  rs485.setTxEcho(echoRawTxPacket);

  connectWifi();

  ipViscaClient.begin(DEFAULT_CAMERA_PORT);
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
  pollInquiryTimeout();
  pollOnePushAfRestore();
  pollCameraModeInquiries();
  updateAutoPowerFromChatter();
  reconcileAutoPower();
}
