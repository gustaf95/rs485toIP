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

// Wi-Fi 연결을 시도한다. 실패해도 자동 AP 모드로 전환하지 않는다 - Serial 메뉴는
// 계속 사용 가능하며, maintainWifi()가 주기적으로 재접속을 시도한다.
void connectWifi() {
  SystemConfig& cfg = routingTable.get();

  // WiFi.mode()는 SSID 유무와 상관없이 항상 먼저 호출한다 - 이것이 lwIP TCP/IP
  // 태스크를 초기화하며, 이걸 건너뛰면 이후 WiFiUDP::begin() 호출 시
  // "tcpip_send_msg_wait_sem ... Invalid mbox" assert로 재부팅 루프에 빠진다.
  WiFi.mode(WIFI_STA);

  if (strlen(cfg.wifi.ssid) == 0) {
    if (serialMenu.isActive()) {
      Serial.println("No Wi-Fi SSID configured. Use Serial menu (Network Settings) to set one.");
    }
    return;
  }

  if (!cfg.wifi.useDhcp) {
    WiFi.config(cfg.wifi.staticIp.toIPAddress(), cfg.wifi.gateway.toIPAddress(),
                cfg.wifi.subnet.toIPAddress());
  }

  if (serialMenu.isActive()) {
    Serial.print("WiFi connecting to ");
    Serial.print(cfg.wifi.ssid);
  }
  WiFi.begin(cfg.wifi.ssid, cfg.wifi.password);
  wifiIsStation = true;

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    if (serialMenu.isActive()) Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    wasWifiConnected = true;
    if (serialMenu.isActive()) {
      Serial.println();
      Serial.print("WiFi connected: ");
      Serial.println(WiFi.localIP());
    }
  } else if (serialMenu.isActive()) {
    Serial.println();
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
    if (serialMenu.isActive()) Serial.println("WiFi disconnected, will retry");
    wasWifiConnected = false;
  }

  unsigned long now = millis();
  if (now - lastWifiRetryMs < kWifiRetryIntervalMs) return;
  lastWifiRetryMs = now;

  WiFi.begin(cfg.wifi.ssid, cfg.wifi.password);

  if (WiFi.status() == WL_CONNECTED) {
    diagnostics.recordWifiReconnect();
    if (serialMenu.isActive()) {
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
  }
  return false;
}

void handleViscaPacket(const uint8_t* data, uint8_t len) {
  SystemConfig& cfg = routingTable.get();
  diagnostics.recordRs485Rx(data, len);
  statusLed.notifyRs485Signal();

  uint8_t addressByte = data[0];
  bool isBroadcast = (addressByte == VISCA_ADDR_BROADCAST);
  bool validStart =
      isBroadcast || (addressByte >= VISCA_ADDR_CAM1 && addressByte <= VISCA_ADDR_CAM7);

  if (!validStart) {
    diagnostics.recordMalformed();
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
    if (!isBroadcast) {
      diagnostics.recordIgnoredNoIp(data, len);
    }
    return;
  }

  for (uint8_t i = 0; i < count; i++) {
    CameraSlot& slot = *results[i].slot;
    uint8_t camNumber = results[i].camNumber;
    IPAddress ip = slot.ip.toIPAddress();
    String target = ip.toString() + ":" + String(slot.port);

    bool ok = sendToCamera(slot, results[i].output, results[i].outputLen);

    if (!isBroadcast) {
      diagnostics.recordForwarded(results[i].output, results[i].outputLen, target);
    }

    if (ok) {
      diagnostics.recordIpTxSuccess();

      if (cfg.responseMode == ResponseMode::SYNTHETIC) {
        sendSyntheticResponse(camNumber);
      }
      // FORWARD / FORWARD_REWRITE 응답은 loop()의 pollCameraResponses()에서 비동기로 처리한다.
    } else {
      diagnostics.recordIpTxFailed();
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
    return;
  }
}

void setup() {
  routingTable.applyDefaults();
  if (!storage.load(routingTable.get())) {
    storage.save(routingTable.get());
  }
  diagnostics.begin();
  statusLed.begin(STATUS_LED_PIN);

  // RS485가 RX0/TX0(UART0)에 고정 결선되어 USB 콘솔과 Serial을 공유하므로,
  // rs485.begin()이 이 baudrate로 Serial을 시작한다 - 별도의 Serial.begin() 없음.
  // 부팅 시에는 아무 메시지도 찍지 않는다 - 사용자가 콘솔에서 Enter를 한 번
  // 입력하기 전까지 serialMenu가 비활성 상태를 유지하며 조용히 있는다.
  SystemConfig& cfg = routingTable.get();
  rs485.begin(cfg.rs485Baudrate, cfg.rs485RxPin, cfg.rs485TxPin, cfg.rs485DeRePin);
  delay(200);

  connectWifi();

  ipViscaClient.begin();
  sonyViscaClient.begin();

  serialMenu.begin();
}

void loop() {
  maintainWifi();
  statusLed.update(WiFi.status() == WL_CONNECTED);

  // RS485(UART0)가 USB 콘솔과 같은 Serial을 공유하므로, 들어온 바이트 하나를
  // 메뉴 파서와 VISCA 파서 양쪽에 동시에 넘긴다. 메뉴 쪽이 먼저 다 읽어가 버리면
  // VISCA 파서가 아무 바이트도 못 보게 되므로 반드시 이렇게 한 곳에서만 읽어야 한다.
  while (rs485.available()) {
    uint8_t b = rs485.read();

    // 유효한 VISCA 주소 바이트(0x81~0x88)로 시작한 패킷이 아직 진행 중일 때만
    // 메뉴 쪽으로 바이트를 넘기지 않는다 - 페이로드 바이트가 우연히
    // Enter(0x0A)/Backspace(0x08, 0x7F)와 같은 값이라서 메뉴가 오작동하는 걸
    // 막기 위함이다. length()>0이라는 이유만으로 막으면, PTZ 컨트롤러가 없어
    // RS485 라인이 떠 있어 노이즈가 계속 들어올 때 메뉴가 영영 반응하지 않게
    // 되므로, 반드시 첫 바이트가 실제 VISCA 주소인지까지 확인해야 한다.
    bool viscaPacketInProgress = viscaParser.length() > 0 &&
                                  viscaParser.buffer()[0] >= VISCA_ADDR_CAM1 &&
                                  viscaParser.buffer()[0] <= VISCA_ADDR_BROADCAST;
    if (!viscaPacketInProgress) {
      serialMenu.feedByte((char)b);
    }

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
