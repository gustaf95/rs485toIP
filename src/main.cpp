#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "RoutingTable.h"
#include "Storage.h"
#include "Diagnostics.h"
#include "ViscaParser.h"
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
IpViscaClient ipViscaClient;
SonyViscaClient sonyViscaClient;
SerialMenu serialMenu(routingTable, storage, diagnostics, rs485, connectWifi);
StatusLed statusLed;

bool wifiIsStation = false;
unsigned long lastWifiRetryMs = 0;
bool wasWifiConnected = false;
const unsigned long kWifiRetryIntervalMs = 5000;

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

  while (rs485.available()) {
    uint8_t b = rs485.read();
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

  if (viscaParser.poll() == ViscaParseResult::TIMEOUT_DISCARD) {
    diagnostics.recordTimeout();
  }

  pollCameraResponses();
}
