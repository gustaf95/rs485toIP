#include <Arduino.h>
#include <WiFi.h>

#include "config.h"
#include "DeviceConfig.h"
#include "Diagnostics.h"
#include "ViscaParser.h"
#include "Rs485Port.h"
#include "DeviceRouter.h"
#include "IpViscaClient.h"
#include "SonyViscaClient.h"
#include "WebConfigServer.h"

DeviceConfig deviceConfig;
Diagnostics diagnostics;
Rs485Port rs485;
ViscaParser viscaParser;
DeviceRouter deviceRouter(deviceConfig);
IpViscaClient ipViscaClient;
SonyViscaClient sonyViscaClient;
WebConfigServer webServer(deviceConfig, diagnostics);

bool wifiIsStation = false;
unsigned long lastWifiRetryMs = 0;
bool wasWifiConnected = false;
const unsigned long kWifiRetryIntervalMs = 5000;

void startFallbackAp() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(AP_FALLBACK_IP, AP_FALLBACK_IP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_FALLBACK_SSID, AP_FALLBACK_PASSWORD);
  wifiIsStation = false;

  Serial.print("Fallback AP started: ");
  Serial.print(AP_FALLBACK_SSID);
  Serial.print(" @ ");
  Serial.println(WiFi.softAPIP());
}

void connectWifi() {
  SystemConfig& cfg = deviceConfig.get();

  if (strlen(cfg.wifi.ssid) == 0) {
    Serial.println("No Wi-Fi SSID configured, entering AP fallback mode");
    startFallbackAp();
    return;
  }

  WiFi.mode(WIFI_STA);
  if (!cfg.wifi.useDhcp) {
    WiFi.config(cfg.wifi.staticIp.toIPAddress(), cfg.wifi.gateway.toIPAddress(),
                cfg.wifi.subnet.toIPAddress());
  }

  Serial.print("WiFi connecting to ");
  Serial.print(cfg.wifi.ssid);
  WiFi.begin(cfg.wifi.ssid, cfg.wifi.password);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    wifiIsStation = true;
    wasWifiConnected = true;
    Serial.print("WiFi connected: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connect failed, entering AP fallback mode");
    startFallbackAp();
  }
}

// STA 모드로 연결을 시도했으나 끊어진 경우, 주기적으로 재접속을 시도한다.
void maintainWifi() {
  if (!wifiIsStation) return;

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

  SystemConfig& cfg = deviceConfig.get();
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

void sendSyntheticResponse(const TargetDevice& device, uint8_t inputAddressByte) {
  uint8_t replyAddr = 0x90 | (inputAddressByte & 0x0F);
  uint8_t ack[3] = {replyAddr, 0x41, VISCA_TERMINATOR};
  uint8_t completion[3] = {replyAddr, 0x51, VISCA_TERMINATOR};

  rs485.writePacket(ack, sizeof(ack));
  rs485.writePacket(completion, sizeof(completion));
}

bool sendToCamera(const TargetDevice& device, const uint8_t* data, uint8_t len) {
  IPAddress ip = device.cameraIp.toIPAddress();
  if (device.protocolMode == ProtocolMode::SONY_VISCA_IP) {
    return sonyViscaClient.send(ip, device.cameraPort, data, len);
  }
  return ipViscaClient.send(ip, device.cameraPort, data, len);
}

void handleViscaPacket(const uint8_t* data, uint8_t len) {
  diagnostics.recordRs485Rx(data, len);
  Serial.print("RS485 RX: ");
  Serial.println(viscaBytesToHex(data, len));

  if ((isPanTiltStop(data, len) || isZoomStop(data, len)) &&
      wifiIsStation && WiFi.status() != WL_CONNECTED) {
    Serial.println("WARNING: Stop command received while WiFi is disconnected - may be lost");
  }

  RoutedPacket results[MAX_TARGET_DEVICES];
  uint8_t count = deviceRouter.route(data, len, results, MAX_TARGET_DEVICES);

  if (count == 0) {
    uint8_t addressByte = data[0];
    String reason = (addressByte == VISCA_ADDR_BROADCAST) ? "broadcast ignored" : "no target device";
    diagnostics.recordIgnored(data, len, reason);
    diagnostics.setLastRoutingResult("ignored");
    return;
  }

  for (uint8_t i = 0; i < count; i++) {
    TargetDevice& device = *results[i].device;
    bool ok = sendToCamera(device, results[i].output, results[i].outputLen);

    String target = String(device.name) + " " + device.cameraIp.toIPAddress().toString() + ":" +
                    String(device.cameraPort);

    if (ok) {
      diagnostics.recordIpTx(results[i].output, results[i].outputLen, target);
      Serial.print("IP VISCA TX -> ");
      Serial.print(target);
      Serial.print(": ");
      Serial.println(viscaBytesToHex(results[i].output, results[i].outputLen));

      diagnostics.setLastRoutingResult(count > 1 ? "broadcast" : (String("sent to ") + device.name));

      if (device.responseMode == ResponseMode::SYNTHETIC) {
        sendSyntheticResponse(device, data[0]);
      }
      // ResponseMode::FORWARD 응답은 loop()의 pollCameraResponses()에서 비동기로 처리한다.
    } else {
      diagnostics.recordTxError();
      Serial.print("IP VISCA TX FAILED -> ");
      Serial.println(target);
    }
  }
}

// 카메라로부터의 응답을 non-blocking으로 확인하여, Response Mode가 forward인
// Target Device의 응답이면 RS485로 그대로 전달한다.
void pollCameraResponses() {
  uint8_t buf[VISCA_BUFFER_SIZE];
  IPAddress remoteIp;

  uint8_t len = ipViscaClient.receive(buf, sizeof(buf), &remoteIp);
  if (len == 0) len = sonyViscaClient.receive(buf, sizeof(buf), &remoteIp);
  if (len == 0) return;

  SystemConfig& cfg = deviceConfig.get();
  for (uint8_t i = 0; i < cfg.deviceCount; i++) {
    TargetDevice& device = cfg.devices[i];
    if (device.responseMode == ResponseMode::FORWARD &&
        device.cameraIp.toIPAddress() == remoteIp) {
      rs485.writePacket(buf, len);
      Serial.print("Forwarded camera response from ");
      Serial.print(remoteIp);
      Serial.print(": ");
      Serial.println(viscaBytesToHex(buf, len));
      return;
    }
  }
}

void setup() {
  Serial.begin(115200);  // UART0: USB Serial 디버그 전용
  delay(200);
  Serial.println();
  Serial.println("ESP32 RS485 VISCA to IP VISCA Gateway starting...");

  deviceConfig.begin();
  diagnostics.begin();

  SystemConfig& cfg = deviceConfig.get();
  rs485.begin(cfg.rs485Baudrate, cfg.rs485RxPin, cfg.rs485TxPin, cfg.rs485DeRePin);

  connectWifi();

  ipViscaClient.begin();
  sonyViscaClient.begin();
  webServer.begin();

  Serial.println("Setup complete");
}

void loop() {
  webServer.handleClient();
  maintainWifi();

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
