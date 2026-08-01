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
#include "SerialMenu.h"
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
SerialMenu serialMenu(routingTable, storage, diagnostics, rs485, connectWifi);
StatusLed statusLed;

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

const char* protocolTag(ProtocolMode mode) {
  switch (mode) {
    case ProtocolMode::IP_VISCA_RAW_UDP: return "UDP";
    case ProtocolMode::IP_VISCA_RAW_TCP: return "TCP";
    case ProtocolMode::SONY_VISCA_UDP: return "SONY_UDP";
  }
  return "?";
}

// Wi-Fi 연결을 시도한다. 실패해도 자동 AP 모드로 전환하지 않는다 - Serial 메뉴는
// 계속 사용 가능하며, maintainWifi()가 주기적으로 재접속을 시도한다.
void connectWifi() {
  SystemConfig& cfg = routingTable.get();

  // WiFi.mode()는 SSID 유무와 상관없이 항상 먼저 호출한다 - 이것이 lwIP TCP/IP
  // 태스크를 초기화하며, 이걸 건너뛰면 이후 WiFiUDP::begin() 호출 시
  // "tcpip_send_msg_wait_sem ... Invalid mbox" assert로 재부팅 루프에 빠진다.
  WiFi.mode(WIFI_STA);

  if (strlen(cfg.wifi.ssid) == 0) {
    Serial.println("No Wi-Fi SSID configured. Use Serial menu (Network Settings) to set one.");
    return;
  }

  if (!cfg.wifi.useDhcp) {
    WiFi.config(cfg.wifi.staticIp.toIPAddress(), cfg.wifi.gateway.toIPAddress(),
                cfg.wifi.subnet.toIPAddress());
  }

  Serial.print("WiFi connecting to ");
  Serial.print(cfg.wifi.ssid);
  WiFi.begin(cfg.wifi.ssid, cfg.wifi.password);
  wifiIsStation = true;

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wasWifiConnected = true;
    Serial.print("WiFi connected: ");
    Serial.println(WiFi.localIP());
  } else {
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
    Serial.println("WiFi disconnected, will retry");
    wasWifiConnected = false;
  }

  unsigned long now = millis();
  if (now - lastWifiRetryMs < kWifiRetryIntervalMs) return;
  lastWifiRetryMs = now;

  WiFi.begin(cfg.wifi.ssid, cfg.wifi.password);

  if (WiFi.status() == WL_CONNECTED) {
    diagnostics.recordWifiReconnect();
    Serial.print("WiFi reconnected: ");
    Serial.println(WiFi.localIP());
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
      WiFi.status() != WL_CONNECTED) {
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

// Pelco-D General Response(ACK)를 합성해서 돌려준다. 체크섬은 원본 명령의 체크섬
// 바이트를 그대로 사용한다 (ALARMS=0x00이므로 sum(원본 CKSM, 0x00) = 원본 CKSM).
void sendPelcoDResponse(const uint8_t* data, uint8_t len) {
  uint8_t response[4] = {PELCO_D_START_BYTE, data[1], 0x00, data[len - 1]};
  rs485.writePacket(response, sizeof(response));
  diagnostics.recordRs485TxResponse();
}

// Pelco-D -> VISCA 명령 변환은 아직 구현되지 않았다. 여기서는 프레이밍/체크섬
// 검증과 General Response(ACK) 회신까지만 수행하고, 실제 라우팅/전송은 없다.
void handlePelcoDPacket(const uint8_t* data, uint8_t len) {
  diagnostics.recordRs485Rx(data, len);
  statusLed.notifyRs485Signal();

  SystemConfig& cfg = routingTable.get();
  if (cfg.debugMode) {
    Serial.print("[PELCO-D RX] ");
    Serial.println(viscaBytesToHex(data, len));
    Serial.println("[ACTION] Pelco-D -> VISCA translation not implemented yet; ignored");
  }

  if (cfg.pelcoResponseMode == PelcoResponseMode::SYNTHETIC) {
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

// Pelco-P -> VISCA 명령 변환은 아직 구현되지 않았다. 여기서는 프레이밍/체크섬
// 검증과 General Response(ACK) 회신까지만 수행하고, 실제 라우팅/전송은 없다.
void handlePelcoPPacket(const uint8_t* data, uint8_t len) {
  diagnostics.recordRs485Rx(data, len);
  statusLed.notifyRs485Signal();

  SystemConfig& cfg = routingTable.get();
  if (cfg.debugMode) {
    Serial.print("[PELCO-P RX] ");
    Serial.println(viscaBytesToHex(data, len));
    Serial.println("[ACTION] Pelco-P -> VISCA translation not implemented yet; ignored");
  }

  if (cfg.pelcoResponseMode == PelcoResponseMode::SYNTHETIC) {
    sendPelcoPResponse(data, len);
    if (cfg.debugMode) {
      Serial.println("[TX] Pelco-P General Response (ACK)");
    }
  }
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
  Serial.begin(9600);  // UART0: USB Serial 메뉴/디버그 전용
  delay(200);
  Serial.println();
  Serial.println("ESP32 RS485 VISCA to IP VISCA Gateway starting...");

  routingTable.applyDefaults();
  if (!storage.load(routingTable.get())) {
    storage.save(routingTable.get());
  }
  diagnostics.begin();
  statusLed.begin(STATUS_LED_PIN);

  SystemConfig& cfg = routingTable.get();
  rs485.begin(cfg.rs485Baudrate, cfg.rs485RxPin, cfg.rs485TxPin, cfg.rs485DeRePin);

  connectWifi();

  ipViscaClient.begin();
  sonyViscaClient.begin();

  serialMenu.begin();

  Serial.println("Setup complete");
}

void loop() {
  serialMenu.poll();
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

  pollCameraResponses();
}
