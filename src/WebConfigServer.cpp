#include "WebConfigServer.h"
#include <WiFi.h>
#include <string.h>
#include <stdlib.h>

namespace {

String protocolModeToString(ProtocolMode m) {
  return m == ProtocolMode::SONY_VISCA_IP ? "SONY_VISCA_IP" : "IP_VISCA_RAW_UDP";
}

ProtocolMode parseProtocolMode(const String& s) {
  return s == "SONY_VISCA_IP" ? ProtocolMode::SONY_VISCA_IP : ProtocolMode::IP_VISCA_RAW_UDP;
}

String outputAddressModeToString(OutputAddressMode m) {
  return m == OutputAddressMode::REWRITE_TO_0x81 ? "rewrite_to_0x81" : "preserve";
}

OutputAddressMode parseOutputAddressMode(const String& s) {
  return s == "preserve" ? OutputAddressMode::PRESERVE : OutputAddressMode::REWRITE_TO_0x81;
}

String responseModeToString(ResponseMode m) {
  switch (m) {
    case ResponseMode::SYNTHETIC: return "synthetic";
    case ResponseMode::FORWARD: return "forward";
    default: return "none";
  }
}

ResponseMode parseResponseMode(const String& s) {
  if (s == "synthetic") return ResponseMode::SYNTHETIC;
  if (s == "forward") return ResponseMode::FORWARD;
  return ResponseMode::NONE;
}

String broadcastModeToString(BroadcastMode m) {
  return m == BroadcastMode::FORWARD_TO_ALL ? "forward_to_all" : "ignore";
}

BroadcastMode parseBroadcastMode(const String& s) {
  return s == "forward_to_all" ? BroadcastMode::FORWARD_TO_ALL : BroadcastMode::IGNORE;
}

String hexByte(uint8_t value) {
  char buf[8];
  snprintf(buf, sizeof(buf), "0x%02X", value);
  return String(buf);
}

}  // namespace

void WebConfigServer::begin() {
  _server.on("/", HTTP_GET, [this]() { handleDashboard(); });
  _server.on("/wifi", HTTP_GET, [this]() { handleWifiGet(); });
  _server.on("/wifi/save", HTTP_POST, [this]() { handleWifiPost(); });
  _server.on("/devices", HTTP_GET, [this]() { handleDevicesGet(); });
  _server.on("/devices/save", HTTP_POST, [this]() { handleDeviceSavePost(); });
  _server.on("/devices/delete", HTTP_POST, [this]() { handleDeviceDeletePost(); });
  _server.on("/diagnostics", HTTP_GET, [this]() { handleDiagnostics(); });
  _server.onNotFound([this]() { handleNotFound(); });
  _server.begin();
}

void WebConfigServer::handleClient() {
  _server.handleClient();
}

String WebConfigServer::navBar() {
  return "<nav><a href=\"/\">Dashboard</a> | "
         "<a href=\"/wifi\">Wi-Fi</a> | "
         "<a href=\"/devices\">Target Devices</a> | "
         "<a href=\"/diagnostics\">Diagnostics</a></nav><hr>";
}

String WebConfigServer::pageShell(const String& title, const String& body) {
  String html;
  html.reserve(body.length() + 512);
  html += "<!doctype html><html><head><meta charset=\"utf-8\">";
  html += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  html += "<title>" + title + "</title>";
  html += "<style>body{font-family:sans-serif;margin:1.5em;max-width:760px}"
          "table{border-collapse:collapse;width:100%}"
          "td,th{border:1px solid #ccc;padding:4px 8px;text-align:left}"
          "label{display:block;margin-top:0.6em}"
          "input,select{width:100%;padding:4px;box-sizing:border-box}"
          "button{margin-top:1em;padding:6px 14px}"
          "nav a{margin-right:4px}</style></head><body>";
  html += "<h2>" + title + "</h2>";
  html += navBar();
  html += body;
  html += "</body></html>";
  return html;
}

void WebConfigServer::handleDashboard() {
  SystemConfig& cfg = _config.get();

  String body;
  body += "<table>";
  body += "<tr><td>ESP32 IP</td><td>" + WiFi.localIP().toString() + "</td></tr>";
  body += "<tr><td>Wi-Fi 연결 상태</td><td>" +
          String(WiFi.status() == WL_CONNECTED ? "연결됨" : "연결 안 됨") + "</td></tr>";
  body += "<tr><td>현재 SSID</td><td>" + WiFi.SSID() + "</td></tr>";
  body += "<tr><td>RS485 Baudrate</td><td>" + String(cfg.rs485Baudrate) + "</td></tr>";
  body += "<tr><td>등록된 Target Device 수</td><td>" + String(cfg.deviceCount) + "</td></tr>";
  body += "<tr><td>마지막 RS485 수신 패킷</td><td>" + _diagnostics.lastRs485Rx() + "</td></tr>";
  body += "<tr><td>마지막 IP 전송 패킷</td><td>" + _diagnostics.lastIpTx() + "</td></tr>";
  body += "<tr><td>마지막 라우팅 결과</td><td>" + _diagnostics.lastRoutingResult() + "</td></tr>";
  body += "<tr><td>RS485 RX Count</td><td>" + String(_diagnostics.rs485RxCount()) + "</td></tr>";
  body += "<tr><td>IP TX Count</td><td>" + String(_diagnostics.ipTxCount()) + "</td></tr>";
  body += "<tr><td>Error Count</td><td>" + String(_diagnostics.errorCount()) + "</td></tr>";
  body += "<tr><td>Uptime</td><td>" + _diagnostics.uptimeString() + "</td></tr>";
  body += "</table>";

  body += "<h3>Target Device 상태</h3><table><tr><th>Name</th><th>Enabled</th><th>Input Addr</th>"
          "<th>IP</th><th>Port</th><th>Protocol</th></tr>";
  for (uint8_t i = 0; i < cfg.deviceCount; i++) {
    TargetDevice& d = cfg.devices[i];
    body += "<tr><td>" + String(d.name) + "</td><td>" + String(d.enabled ? "true" : "false") +
            "</td><td>" + hexByte(d.inputAddressByte) + "</td><td>" +
            d.cameraIp.toIPAddress().toString() + "</td><td>" + String(d.cameraPort) +
            "</td><td>" + protocolModeToString(d.protocolMode) + "</td></tr>";
  }
  body += "</table>";

  _server.send(200, "text/html", pageShell("Dashboard", body));
}

void WebConfigServer::handleWifiGet() {
  SystemConfig& cfg = _config.get();

  String body = "<form method=\"POST\" action=\"/wifi/save\">";
  body += "<label>SSID<input name=\"ssid\" value=\"" + String(cfg.wifi.ssid) + "\"></label>";
  body += "<label>Password<input name=\"password\" type=\"password\" value=\"" +
          String(cfg.wifi.password) + "\"></label>";
  body += "<label>DHCP 사용<select name=\"dhcp\">";
  body += String("<option value=\"1\"") + (cfg.wifi.useDhcp ? " selected" : "") + ">DHCP</option>";
  body += String("<option value=\"0\"") + (!cfg.wifi.useDhcp ? " selected" : "") + ">Static IP</option>";
  body += "</select></label>";
  body += "<label>Static IP<input name=\"static_ip\" value=\"" +
          cfg.wifi.staticIp.toIPAddress().toString() + "\"></label>";
  body += "<label>Gateway<input name=\"gateway\" value=\"" +
          cfg.wifi.gateway.toIPAddress().toString() + "\"></label>";
  body += "<label>Subnet Mask<input name=\"subnet\" value=\"" +
          cfg.wifi.subnet.toIPAddress().toString() + "\"></label>";
  body += "<button type=\"submit\">저장하고 재부팅</button>";
  body += "</form>";
  body += "<p>Wi-Fi 접속 실패 시 fallback AP <b>" AP_FALLBACK_SSID "</b> (192.168.4.1)로 진입합니다.</p>";

  _server.send(200, "text/html", pageShell("Wi-Fi 설정", body));
}

void WebConfigServer::handleWifiPost() {
  SystemConfig& cfg = _config.get();

  String ssid = _server.arg("ssid");
  String password = _server.arg("password");
  strncpy(cfg.wifi.ssid, ssid.c_str(), sizeof(cfg.wifi.ssid) - 1);
  cfg.wifi.ssid[sizeof(cfg.wifi.ssid) - 1] = '\0';
  strncpy(cfg.wifi.password, password.c_str(), sizeof(cfg.wifi.password) - 1);
  cfg.wifi.password[sizeof(cfg.wifi.password) - 1] = '\0';

  cfg.wifi.useDhcp = _server.arg("dhcp") == "1";

  IPAddress ip;
  if (ip.fromString(_server.arg("static_ip"))) cfg.wifi.staticIp.fromIPAddress(ip);
  if (ip.fromString(_server.arg("gateway"))) cfg.wifi.gateway.fromIPAddress(ip);
  if (ip.fromString(_server.arg("subnet"))) cfg.wifi.subnet.fromIPAddress(ip);

  _config.save();

  String body = "<p>Wi-Fi 설정이 저장되었습니다. 재부팅합니다...</p>";
  _server.send(200, "text/html", pageShell("Wi-Fi 저장됨", body));
  delay(300);
  ESP.restart();
}

void WebConfigServer::handleDevicesGet() {
  SystemConfig& cfg = _config.get();

  String body = "<h3>등록된 Target Device</h3><table><tr><th>Name</th><th>ID</th><th>Input Addr</th>"
                "<th>Output Mode</th><th>IP</th><th>Port</th><th>Protocol</th><th>Response</th>"
                "<th>Enabled</th><th></th></tr>";
  for (uint8_t i = 0; i < cfg.deviceCount; i++) {
    TargetDevice& d = cfg.devices[i];
    body += "<tr><td>" + String(d.name) + "</td><td>" + String(d.rs485ViscaId) + "</td><td>" +
            hexByte(d.inputAddressByte) + "</td><td>" +
            outputAddressModeToString(d.outputAddressMode) + "</td><td>" +
            d.cameraIp.toIPAddress().toString() + "</td><td>" + String(d.cameraPort) + "</td><td>" +
            protocolModeToString(d.protocolMode) + "</td><td>" +
            responseModeToString(d.responseMode) + "</td><td>" +
            String(d.enabled ? "true" : "false") + "</td>"
            "<td><form method=\"POST\" action=\"/devices/delete\" onsubmit=\"return confirm('삭제하시겠습니까?');\">"
            "<input type=\"hidden\" name=\"rs485_id\" value=\"" + String(d.rs485ViscaId) +
            "\"><button type=\"submit\">삭제</button></form></td></tr>";
  }
  body += "</table>";

  body += "<h3>Broadcast(0x88) 처리</h3><form method=\"POST\" action=\"/devices/save\">"
          "<input type=\"hidden\" name=\"broadcast_only\" value=\"1\">"
          "<label>Broadcast Mode<select name=\"broadcast_mode\">";
  body += String("<option value=\"ignore\"") +
          (cfg.broadcastMode == BroadcastMode::IGNORE ? " selected" : "") + ">ignore</option>";
  body += String("<option value=\"forward_to_all\"") +
          (cfg.broadcastMode == BroadcastMode::FORWARD_TO_ALL ? " selected" : "") +
          ">forward_to_all</option>";
  body += "</select></label><button type=\"submit\">저장</button></form>";

  body += "<h3>Target Device 추가 / 수정</h3>"
          "<p>기존 RS485 VISCA ID를 입력하면 해당 Device가 수정됩니다.</p>"
          "<form method=\"POST\" action=\"/devices/save\">";
  body += "<label>Device Name<input name=\"name\" maxlength=\"15\" value=\"CAM5\"></label>";
  body += "<label>Enabled<select name=\"enabled\"><option value=\"1\">true</option>"
          "<option value=\"0\">false</option></select></label>";
  body += "<label>RS485 VISCA ID (1-255)<input name=\"rs485_id\" type=\"number\" min=\"1\" max=\"255\" value=\"5\"></label>";
  body += "<label>Input VISCA Address Byte (hex, 예: 85)<input name=\"input_addr\" value=\"85\"></label>";
  body += "<label>Output VISCA Address Mode<select name=\"output_mode\">"
          "<option value=\"rewrite_to_0x81\">rewrite_to_0x81</option>"
          "<option value=\"preserve\">preserve</option></select></label>";
  body += "<label>Camera IP<input name=\"camera_ip\" placeholder=\"192.168.1.101\"></label>";
  body += "<label>Camera Port<input name=\"camera_port\" type=\"number\" value=\"5678\"></label>";
  body += "<label>Protocol Mode<select name=\"protocol_mode\">"
          "<option value=\"IP_VISCA_RAW_UDP\">IP_VISCA_RAW_UDP</option>"
          "<option value=\"SONY_VISCA_IP\">SONY_VISCA_IP</option></select></label>";
  body += "<label>Response Mode<select name=\"response_mode\">"
          "<option value=\"synthetic\">synthetic</option>"
          "<option value=\"none\">none</option>"
          "<option value=\"forward\">forward</option></select></label>";
  body += "<button type=\"submit\">저장</button></form>";

  _server.send(200, "text/html", pageShell("Target Device 설정", body));
}

void WebConfigServer::handleDeviceSavePost() {
  SystemConfig& cfg = _config.get();

  if (_server.hasArg("broadcast_only")) {
    cfg.broadcastMode = parseBroadcastMode(_server.arg("broadcast_mode"));
    _config.save();
    _server.sendHeader("Location", "/devices");
    _server.send(303);
    return;
  }

  TargetDevice d = {};
  String name = _server.arg("name");
  strncpy(d.name, name.c_str(), sizeof(d.name) - 1);

  d.enabled = _server.arg("enabled") == "1";
  d.rs485ViscaId = (uint8_t)_server.arg("rs485_id").toInt();

  long inputAddr = strtol(_server.arg("input_addr").c_str(), nullptr, 16);
  d.inputAddressByte = (uint8_t)inputAddr;

  d.outputAddressMode = parseOutputAddressMode(_server.arg("output_mode"));

  IPAddress ip;
  ip.fromString(_server.arg("camera_ip"));
  d.cameraIp.fromIPAddress(ip);

  d.cameraPort = (uint16_t)_server.arg("camera_port").toInt();
  d.protocolMode = parseProtocolMode(_server.arg("protocol_mode"));
  d.responseMode = parseResponseMode(_server.arg("response_mode"));

  _config.addOrUpdateDevice(d);
  _config.save();

  _server.sendHeader("Location", "/devices");
  _server.send(303);
}

void WebConfigServer::handleDeviceDeletePost() {
  uint8_t id = (uint8_t)_server.arg("rs485_id").toInt();
  _config.deleteDevice(id);
  _config.save();

  _server.sendHeader("Location", "/devices");
  _server.send(303);
}

void WebConfigServer::handleDiagnostics() {
  String body = "<table><tr><td>잘못된 패킷 수</td><td>" + String(_diagnostics.malformedCount()) +
                "</td></tr><tr><td>버퍼 오버플로우 수</td><td>" + String(_diagnostics.overflowCount()) +
                "</td></tr><tr><td>Packet timeout 수</td><td>" + String(_diagnostics.timeoutCount()) +
                "</td></tr><tr><td>Wi-Fi 재접속 횟수</td><td>" + String(_diagnostics.wifiReconnectCount()) +
                "</td></tr><tr><td>Uptime</td><td>" + _diagnostics.uptimeString() + "</td></tr></table>";

  body += "<h3>최근 RS485 수신 로그</h3><ul>";
  for (uint8_t i = 0; i < DIAG_LOG_DEPTH; i++) {
    if (_diagnostics.rxLog()[i].length() == 0) continue;
    body += "<li>" + _diagnostics.rxLog()[i] + "</li>";
  }
  body += "</ul>";

  body += "<h3>최근 IP 전송 로그</h3><ul>";
  for (uint8_t i = 0; i < DIAG_LOG_DEPTH; i++) {
    if (_diagnostics.txLog()[i].length() == 0) continue;
    body += "<li>" + _diagnostics.txLog()[i] + "</li>";
  }
  body += "</ul>";

  body += "<h3>최근 무시된 패킷 로그</h3><ul>";
  for (uint8_t i = 0; i < DIAG_LOG_DEPTH; i++) {
    if (_diagnostics.ignoredLog()[i].length() == 0) continue;
    body += "<li>" + _diagnostics.ignoredLog()[i] + "</li>";
  }
  body += "</ul>";

  _server.send(200, "text/html", pageShell("Diagnostics", body));
}

void WebConfigServer::handleNotFound() {
  _server.send(404, "text/plain", "Not found");
}
