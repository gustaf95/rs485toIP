#include "WebConfigServer.h"
#include <WiFi.h>
#include "Rs485PinValidation.h"
#include "GatewayActions.h"

namespace {
String htmlEscape(const String& in) {
  String out;
  out.reserve(in.length());
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out += c;
    }
  }
  return out;
}

String selectOption(int value, int current, const String& label) {
  String html = "<option value=\"" + String(value) + "\"";
  if (value == current) html += " selected";
  html += ">" + label + "</option>";
  return html;
}

const uint32_t kBaudChoices[5] = {2400, 4800, 9600, 38400, 115200};
}  // namespace

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------

void WebConfigServer::begin() {
  SystemConfig& cfg = _routing.get();

  // apSsid가 비어있으면(최초 부팅 또는 Factory Reset 직후) MAC 주소 뒷자리를 붙인
  // 기본값을 한 번 만들어서 flash에 저장한다 - 그 뒤로는 항상 저장된 값을 그대로
  // 쓴다(RoutingTable.h 주석 참고). 이 시점에는 connectWifi()가 이미
  // WiFi.mode(WIFI_AP_STA)를 호출한 뒤라 MAC 주소를 안전하게 읽을 수 있다.
  if (strlen(cfg.wifi.apSsid) == 0) {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    String defaultSsid = String(AP_SSID_PREFIX) + mac.substring(mac.length() - 4);
    defaultSsid.toCharArray(cfg.wifi.apSsid, sizeof(cfg.wifi.apSsid));
    _storage.save(cfg);
  }
  applyApSettings(cfg);

  _server.on("/", HTTP_GET, [this]() { handleStatus(); });
  _server.on("/network", HTTP_GET, [this]() { handleNetworkGet(); });
  _server.on("/network", HTTP_POST, [this]() { handleNetworkPost(); });
  _server.on("/network/retry", HTTP_POST, [this]() { handleNetworkRetry(); });
  _server.on("/network/ap", HTTP_POST, [this]() { handleNetworkApPost(); });
  _server.on("/rs485", HTTP_GET, [this]() { handleRs485Get(); });
  _server.on("/rs485", HTTP_POST, [this]() { handleRs485Post(); });
  _server.on("/rs485/uart0", HTTP_GET, [this]() { handleRs485Uart0Get(); });
  _server.on("/rs485/uart0", HTTP_POST, [this]() { handleRs485Uart0Post(); });
  _server.on("/routing", HTTP_GET, [this]() { handleRoutingGet(); });
  _server.on("/routing/cam", HTTP_GET, [this]() { handleRoutingCamGet(); });
  _server.on("/routing/cam", HTTP_POST, [this]() { handleRoutingCamPost(); });
  _server.on("/counters", HTTP_GET, [this]() { handleCountersGet(); });
  _server.on("/debug", HTTP_GET, [this]() { handleDebugGet(); });
  _server.on("/debug/toggle", HTTP_POST, [this]() { handleDebugTogglePost(); });
  _server.on("/debug/live", HTTP_GET, [this]() { handleDebugLiveGet(); });
  _server.on("/debug/raw", HTTP_GET, [this]() { handleDebugRawGet(); });
  _server.on("/debug/test-command", HTTP_POST, [this]() { handleDebugTestCommandPost(); });
  _server.on("/factory-reset", HTTP_GET, [this]() { handleFactoryResetGet(); });
  _server.on("/factory-reset", HTTP_POST, [this]() { handleFactoryResetPost(); });
  _server.onNotFound([this]() { handleNotFound(); });

  _server.begin();
}

void WebConfigServer::poll() {
  _server.handleClient();
}

// ---------------------------------------------------------------------------
// Rendering helpers
// ---------------------------------------------------------------------------

void WebConfigServer::sendPage(const String& title, const String& bodyHtml, uint16_t refreshSeconds) {
  String html;
  html += "<!DOCTYPE html><html><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";
  if (refreshSeconds > 0) {
    html += "<meta http-equiv=\"refresh\" content=\"" + String(refreshSeconds) + "\">";
  }
  html += "<title>" + htmlEscape(title) + "</title><style>";
  html += "body{font-family:sans-serif;max-width:640px;margin:1em auto;padding:0 1em;line-height:1.5}";
  html += "nav{margin-bottom:1em}nav a{margin-right:0.8em}";
  html += "table{border-collapse:collapse;width:100%;margin:0.5em 0}";
  html += "td,th{border:1px solid #ccc;padding:4px 8px;text-align:left}";
  html += "label{display:block;margin:0.6em 0}";
  html += "input,select{width:100%;max-width:320px;box-sizing:border-box;padding:4px}";
  html += "button,input[type=submit]{padding:6px 12px;margin-top:0.5em}";
  html += "pre{background:#f4f4f4;padding:0.6em;overflow-x:auto}";
  html += "</style></head><body>";
  html += "<nav><a href=\"/\">Status</a><a href=\"/network\">Network</a><a href=\"/rs485\">RS485</a>"
          "<a href=\"/routing\">Routing</a><a href=\"/counters\">Counters</a><a href=\"/debug\">Debug</a>"
          "<a href=\"/factory-reset\">Factory Reset</a></nav><hr>";
  html += "<h2>" + htmlEscape(title) + "</h2>";
  html += bodyHtml;
  html += "</body></html>";
  _server.send(200, "text/html", html);
}

void WebConfigServer::redirectTo(const String& path) {
  _server.sendHeader("Location", path, true);
  _server.send(303);
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------

void WebConfigServer::handleStatus() {
  SystemConfig& cfg = _routing.get();
  bool connected = WiFi.status() == WL_CONNECTED;

  String body = "<table>";
  body += "<tr><td>Mode</td><td>Gateway Running</td></tr>";
  body += "<tr><td>Wi-Fi (STA)</td><td>" + String(connected ? "Connected" : "Disconnected") + "</td></tr>";
  body += "<tr><td>ESP32 IP (STA)</td><td>" +
          String(connected ? WiFi.localIP().toString() : "Not assigned") + "</td></tr>";
  body += "<tr><td>AP SSID</td><td>" + htmlEscape(WiFi.softAPSSID()) + "</td></tr>";
  body += "<tr><td>AP IP</td><td>" + WiFi.softAPIP().toString() + "</td></tr>";
  body += "<tr><td>Debug Mode</td><td>" + String(cfg.debugMode ? "ON" : "OFF") + "</td></tr>";
  body += "</table>";

  sendPage("Status", body);
}

// ---------------------------------------------------------------------------
// Network Settings
// ---------------------------------------------------------------------------

void WebConfigServer::handleNetworkGet() {
  SystemConfig& cfg = _routing.get();
  bool connected = WiFi.status() == WL_CONNECTED;

  int found = WiFi.scanNetworks();
  String scanOptions = "<option value=\"\">-- select scanned network --</option>";
  if (found > 0) {
    for (int i = 0; i < found; i++) {
      String ssid = WiFi.SSID(i);
      scanOptions += "<option value=\"" + htmlEscape(ssid) + "\">" + htmlEscape(ssid) + " (" +
                      String(WiFi.RSSI(i)) + "dBm)</option>";
    }
    WiFi.scanDelete();
  }

  String body = "<table>";
  body += "<tr><td>Wi-Fi Status</td><td>" + String(connected ? "Connected" : "Disconnected") + "</td></tr>";
  body += "<tr><td>Current SSID</td><td>" +
          htmlEscape(strlen(cfg.wifi.ssid) ? cfg.wifi.ssid : "(not set)") + "</td></tr>";
  body += "<tr><td>ESP32 IP (STA)</td><td>" +
          String(connected ? WiFi.localIP().toString() : "Not assigned") + "</td></tr>";
  body += "</table>";
  body += "<p><i>Status LED pin is configured under RS485 Settings.</i></p>";

  body += "<form method=\"POST\" action=\"/network\">";
  body += "<label>SSID (scanned): <select name=\"ssid_scan\">" + scanOptions + "</select></label>";
  body += "<label>Or enter SSID manually: <input type=\"text\" name=\"ssid_manual\" "
          "placeholder=\"leave blank to use dropdown\"></label>";
  body += "<label>Password (leave blank to keep current): <input type=\"password\" name=\"password\"></label>";
  body += "<label><input type=\"checkbox\" name=\"dhcp\" style=\"width:auto\"" +
          String(cfg.wifi.useDhcp ? " checked" : "") + "> Use DHCP</label>";
  body += "<label>Static IP: <input type=\"text\" name=\"static_ip\" value=\"" +
          cfg.wifi.staticIp.toIPAddress().toString() + "\"></label>";
  body += "<label>Gateway: <input type=\"text\" name=\"gateway\" value=\"" +
          cfg.wifi.gateway.toIPAddress().toString() + "\"></label>";
  body += "<label>Subnet: <input type=\"text\" name=\"subnet\" value=\"" +
          cfg.wifi.subnet.toIPAddress().toString() + "\"></label>";
  body += "<button type=\"submit\">Save</button>";
  body += "</form>";

  body += "<form method=\"POST\" action=\"/network/retry\">"
          "<button type=\"submit\">Retry Wi-Fi Connection</button></form>";

  body += "<h3>Access Point (AP)</h3>";
  body += "<table><tr><td>AP IP</td><td>" + WiFi.softAPIP().toString() + "</td></tr></table>";
  body += "<form method=\"POST\" action=\"/network/ap\">";
  body += "<label>AP SSID: <input type=\"text\" name=\"ap_ssid\" value=\"" +
          htmlEscape(cfg.wifi.apSsid) + "\"></label>";
  body += "<label>AP Password (leave blank to keep current, min 8 chars for WPA2): "
          "<input type=\"password\" name=\"ap_password\"></label>";
  body += "<button type=\"submit\">Save AP Settings</button></form>";

  sendPage("Network Settings", body);
}

void WebConfigServer::handleNetworkPost() {
  SystemConfig& cfg = _routing.get();

  String manual = _server.arg("ssid_manual");
  String scanSel = _server.arg("ssid_scan");
  String ssid = manual.length() > 0 ? manual : scanSel;
  if (ssid.length() > 0) {
    ssid.toCharArray(cfg.wifi.ssid, sizeof(cfg.wifi.ssid));
  }

  String pw = _server.arg("password");
  if (pw.length() > 0) {
    pw.toCharArray(cfg.wifi.password, sizeof(cfg.wifi.password));
  }

  cfg.wifi.useDhcp = _server.hasArg("dhcp");

  IPAddress ip;
  if (ip.fromString(_server.arg("static_ip"))) cfg.wifi.staticIp.fromIPAddress(ip);
  if (ip.fromString(_server.arg("gateway"))) cfg.wifi.gateway.fromIPAddress(ip);
  if (ip.fromString(_server.arg("subnet"))) cfg.wifi.subnet.fromIPAddress(ip);

  _storage.save(cfg);
  redirectTo("/network");
}

void WebConfigServer::handleNetworkApPost() {
  SystemConfig& cfg = _routing.get();

  String ssid = _server.arg("ap_ssid");
  ssid.trim();
  if (ssid.length() > 0) {
    ssid.toCharArray(cfg.wifi.apSsid, sizeof(cfg.wifi.apSsid));
  }

  String pw = _server.arg("ap_password");
  if (pw.length() > 0) {
    pw.toCharArray(cfg.wifi.apPassword, sizeof(cfg.wifi.apPassword));
  }

  _storage.save(cfg);
  applyApSettings(cfg);
  redirectTo("/network");
}

void WebConfigServer::handleNetworkRetry() {
  if (_wifiRetry) _wifiRetry();
  redirectTo("/network");
}

// ---------------------------------------------------------------------------
// RS485 Settings
// ---------------------------------------------------------------------------

String WebConfigServer::rs485PageBody(const String& error) {
  SystemConfig& cfg = _routing.get();
  String body;

  if (error.length() > 0) {
    body += "<p style=\"color:red\"><b>Error:</b> " + htmlEscape(error) + "</p>";
  }
  if (_server.hasArg("warn") && _server.arg("warn") == "strap") {
    body += "<p style=\"color:#b8860b\"><b>Warning:</b> one or more pins is a boot strapping pin - "
            "verify no external pull affects boot.</p>";
  }

  body += "<p><b>UART Port:</b> " +
          String(cfg.rs485Uart0Shared ? "UART0 (shared with USB console)" : "UART2 / Serial2") +
          "</p>";

  body += "<form method=\"POST\" action=\"/rs485\">";

  body += "<label>Baudrate: <select name=\"baudrate\">";
  for (uint8_t i = 0; i < 5; i++) {
    body += selectOption((int)kBaudChoices[i], (int)cfg.rs485Baudrate, String(kBaudChoices[i]));
  }
  body += "</select></label>";

  if (cfg.rs485Uart0Shared) {
    // 이 모드에서는 RX/TX/DE-RE가 보드 고정 배선값이라 편집할 수 없다 - 값을 그대로
    // 보여만 주고, hidden 필드로 POST에 실어서 handleRs485Post()가 그대로 유지하게 한다.
    body += "<p>RX Pin: GPIO" + String(cfg.rs485RxPin) + " (fixed)</p>";
    body += "<p>TX Pin: GPIO" + String(cfg.rs485TxPin) + " (fixed)</p>";
    body += "<p>DE/RE Pin: GPIO" + String(cfg.rs485DeRePin) + " (fixed)</p>";
  } else {
    body += "<label>RX Pin: <input type=\"number\" name=\"rx_pin\" value=\"" +
            String(cfg.rs485RxPin) + "\"></label>";
    body += "<label>TX Pin: <input type=\"number\" name=\"tx_pin\" value=\"" +
            String(cfg.rs485TxPin) + "\"></label>";
    body += "<label>DE/RE Pin: <input type=\"number\" name=\"dere_pin\" value=\"" +
            String(cfg.rs485DeRePin) + "\"></label>";
  }

  body += "<label>Signal Inversion: <select name=\"invert\">";
  body += selectOption(1, cfg.rs485Invert ? 1 : 0, "Inverted (A/B swapped wiring)");
  body += selectOption(0, cfg.rs485Invert ? 1 : 0, "Normal");
  body += "</select></label>";
  if (cfg.rs485Uart0Shared) {
    body += "<p><i>In UART0 Shared Mode this also inverts the USB console signals.</i></p>";
  }

  body += "<label>Input Protocol: <select name=\"input_protocol\">";
  body += selectOption(0, (int)cfg.inputProtocol, "VISCA");
  body += selectOption(1, (int)cfg.inputProtocol, "Pelco-D");
  body += selectOption(2, (int)cfg.inputProtocol, "Pelco-P");
  body += selectOption(3, (int)cfg.inputProtocol, "Pelco-D/P Autodetect");
  body += selectOption(4, (int)cfg.inputProtocol, "Raw Bridge");
  body += "</select></label>";

  body += "<label>Pelco Response Mode: <select name=\"pelco_response\">";
  body += selectOption(0, (int)cfg.pelcoResponseMode, "Respond (synthetic ACK)");
  body += selectOption(1, (int)cfg.pelcoResponseMode, "No response");
  body += "</select></label>";

  body += "<label>Camera Response Mode: <select name=\"response_mode\">";
  body += selectOption(0, (int)cfg.responseMode, "none - drop camera responses");
  body += selectOption(1, (int)cfg.responseMode, "synthetic - fake ACK/Completion (VISCA input only)");
  body += selectOption(2, (int)cfg.responseMode, "forward - camera bytes to RS485 as-is");
  body += selectOption(3, (int)cfg.responseMode, "forward_rewrite - forward, rewrite address to 0x9n");
  body += "</select></label>";
  body += "<p><i>forward/forward_rewrite send RAW VISCA bytes. A Pelco-D/P controller cannot "
          "parse those - repackaging into a Pelco response is not implemented yet.</i></p>";

  body += "<label>Status LED Pin: <input type=\"number\" name=\"led_pin\" value=\"" +
          String(cfg.statusLedPin) + "\"></label>";

  body += "<button type=\"submit\">Save</button></form>";

  body += "<p>";
  if (cfg.rs485Uart0Shared) {
    body += "<a href=\"/rs485/uart0\">Switch back to UART2 (independent RS485 port)</a>";
  } else {
    body += "<a href=\"/rs485/uart0\">Switch to UART0 Shared Mode (board wires RS485 onto RX0/TX0)</a>";
  }
  body += "</p>";

  return body;
}

void WebConfigServer::handleRs485Get() {
  sendPage("RS485 Settings", rs485PageBody(""));
}

void WebConfigServer::handleRs485Post() {
  SystemConfig& cfg = _routing.get();
  String error;

  int ledPin = _server.arg("led_pin").toInt();
  if (!validateStatusLedPin(ledPin, cfg.rs485RxPin, cfg.rs485TxPin, cfg.rs485DeRePin, &error)) {
    sendPage("RS485 Settings", rs485PageBody(error));
    return;
  }

  // UART0 Shared Mode에서는 RX/TX/DE-RE가 보드 고정 배선값이라 폼에 입력란 자체가
  // 없다(rs485PageBody() 참고) - 검증 없이 현재 cfg 값을 그대로 유지한다.
  bool anyStrapping = false;
  if (!cfg.rs485Uart0Shared) {
    int rx = _server.arg("rx_pin").toInt();
    int tx = _server.arg("tx_pin").toInt();
    int dere = _server.arg("dere_pin").toInt();

    if (!validateRs485Pin(rx, /*requireOutput=*/false, cfg.statusLedPin, &error) ||
        !validateRs485Pin(tx, /*requireOutput=*/true, cfg.statusLedPin, &error) ||
        !validateRs485Pin(dere, /*requireOutput=*/true, cfg.statusLedPin, &error)) {
      sendPage("RS485 Settings", rs485PageBody(error));
      return;
    }

    anyStrapping = isStrappingGpio((uint8_t)rx) || isStrappingGpio((uint8_t)tx) ||
                   isStrappingGpio((uint8_t)dere);

    cfg.rs485RxPin = (uint8_t)rx;
    cfg.rs485TxPin = (uint8_t)tx;
    cfg.rs485DeRePin = (uint8_t)dere;
  }

  cfg.rs485Baudrate = (uint32_t)_server.arg("baudrate").toInt();
  cfg.rs485Invert = (_server.arg("invert").toInt() != 0);
  cfg.inputProtocol = (InputProtocol)_server.arg("input_protocol").toInt();
  cfg.pelcoResponseMode = (PelcoResponseMode)_server.arg("pelco_response").toInt();
  cfg.responseMode = (ResponseMode)_server.arg("response_mode").toInt();
  cfg.statusLedPin = (uint8_t)ledPin;

  _storage.save(cfg);
  _rs485.begin(cfg.rs485Baudrate, cfg.rs485RxPin, cfg.rs485TxPin, cfg.rs485DeRePin,
               cfg.rs485Uart0Shared, cfg.rs485Invert);
  _statusLed.begin(cfg.statusLedPin);

  redirectTo(anyStrapping ? "/rs485?warn=strap" : "/rs485");
}

// ---------------------------------------------------------------------------
// RS485 UART0 Shared Mode toggle
// ---------------------------------------------------------------------------

void WebConfigServer::handleRs485Uart0Get() {
  SystemConfig& cfg = _routing.get();
  String body;

  if (cfg.rs485Uart0Shared) {
    body += "<p>This switches RS485 back to an independent UART2 port (GPIO" +
            String(RS485_RX_PIN_DEFAULT) + "/" + String(RS485_TX_PIN_DEFAULT) + "/" +
            String(RS485_DE_RE_PIN_DEFAULT) + ") and reboots.</p>";
  } else {
    body += "<p style=\"color:red\"><b>WARNING:</b> This switches RS485 onto UART0 (GPIO3 RX0 / "
            "GPIO1 TX0), sharing it with the USB console. After this, the Serial menu becomes "
            "permanently unavailable (there is no way to tell keystrokes apart from RS485 "
            "traffic) - all further configuration must be done from this Web Config page. "
            "Packet-level Serial debug logging is also disabled in this mode. The device "
            "reboots immediately after confirming.</p>";
  }
  body += "<form method=\"POST\" action=\"/rs485/uart0\"><button type=\"submit\">Confirm"
          "</button></form>";

  sendPage("RS485 Settings", body);
}

void WebConfigServer::handleRs485Uart0Post() {
  bool enabling = !_routing.get().rs485Uart0Shared;
  sendPage("RS485 Settings", enabling
                                  ? "<p>Switching to UART0 Shared Mode. Rebooting - reconnect to "
                                    "this AP and reload /rs485 to verify.</p>"
                                  : "<p>Switching back to UART2. Rebooting...</p>");
  setRs485Uart0SharedMode(_routing, _storage, _rs485, enabling);
}

// ---------------------------------------------------------------------------
// Routing Table
// ---------------------------------------------------------------------------

void WebConfigServer::handleRoutingGet() {
  static const char* kProtocolNames[] = {"IP_VISCA_RAW_UDP", "IP_VISCA_RAW_TCP", "SONY_VISCA_UDP",
                                          "RAW_DATA_UDP"};
  static const char* kAddrModeNames[] = {"rewrite_0x81", "preserve", "rewrite_by_cam"};

  String body = "<table><tr><th>CAM</th><th>VISCA Addr</th><th>IP</th><th>Port</th>"
                "<th>Protocol</th><th>Address Mode</th><th>Auto Power</th></tr>";
  for (uint8_t n = 1; n <= CAMERA_SLOT_COUNT; n++) {
    CameraSlot* slot = _routing.camera(n);
    body += "<tr><td><a href=\"/routing/cam?n=" + String(n) + "\">CAM" + String(n) + "</a></td>";
    body += "<td>0x" + String(0x80 | n, HEX) + "</td>";
    body += "<td>" + (slot->isConfigured() ? slot->ip.toIPAddress().toString() : String("-")) + "</td>";
    body += "<td>" + String(slot->port) + "</td>";
    body += "<td>" + String(kProtocolNames[(int)slot->protocol]) + "</td>";
    body += "<td>" + String(kAddrModeNames[(int)slot->addressMode]) + "</td>";
    body += "<td>" + String(slot->autoPowerControl ? "On" : "Off") + "</td></tr>";
  }
  body += "</table>";

  sendPage("Routing Table", body);
}

void WebConfigServer::handleRoutingCamGet() {
  int n = _server.arg("n").toInt();
  CameraSlot* slot = _routing.camera((uint8_t)n);
  if (!slot) {
    redirectTo("/routing");
    return;
  }

  String body = "<p>VISCA Address: 0x" + String(0x80 | n, HEX) + "</p>";
  body += "<form method=\"POST\" action=\"/routing/cam\">";
  body += "<input type=\"hidden\" name=\"n\" value=\"" + String(n) + "\">";
  body += "<label>Camera IP (blank to clear): <input type=\"text\" name=\"ip\" value=\"" +
          (slot->isConfigured() ? slot->ip.toIPAddress().toString() : String("")) + "\"></label>";
  body += "<label>Port: <input type=\"number\" name=\"port\" value=\"" + String(slot->port) +
          "\"></label>";

  body += "<label>Protocol: <select name=\"protocol\">";
  body += selectOption(0, (int)slot->protocol, "IP_VISCA_RAW_UDP");
  body += selectOption(1, (int)slot->protocol, "IP_VISCA_RAW_TCP");
  body += selectOption(2, (int)slot->protocol, "SONY_VISCA_UDP");
  body += selectOption(3, (int)slot->protocol, "RAW_DATA_UDP (Raw Bridge peer)");
  body += "</select></label>";

  body += "<label>Address Mode: <select name=\"address_mode\">";
  body += selectOption(0, (int)slot->addressMode, "rewrite_0x81");
  body += selectOption(1, (int)slot->addressMode, "preserve");
  body += selectOption(2, (int)slot->addressMode, "rewrite_by_cam");
  body += "</select></label>";

  body += "<label><input type=\"checkbox\" name=\"auto_power_control\"";
  if (slot->autoPowerControl) body += " checked";
  body += "> Auto Power Control (Standby when RS485 goes quiet, power on when it chatters)</label>";

  body += "<button type=\"submit\">Save</button></form>";

  sendPage("CAM" + String(n), body);
}

void WebConfigServer::handleRoutingCamPost() {
  int n = _server.arg("n").toInt();
  CameraSlot* slot = _routing.camera((uint8_t)n);
  if (!slot) {
    redirectTo("/routing");
    return;
  }

  String ipStr = _server.arg("ip");
  ipStr.trim();
  if (ipStr.length() == 0) {
    slot->ip.fromIPAddress(IPAddress(0, 0, 0, 0));
  } else {
    IPAddress ip;
    if (ip.fromString(ipStr)) {
      slot->ip.fromIPAddress(ip);
    }
  }

  int port = _server.arg("port").toInt();
  if (port > 0 && port <= 65535) {
    slot->port = (uint16_t)port;
  }

  slot->protocol = (ProtocolMode)_server.arg("protocol").toInt();
  slot->addressMode = (AddressMode)_server.arg("address_mode").toInt();
  // 체크박스는 체크됐을 때만 폼에 포함된다 - hasArg()로 존재 여부만 본다.
  slot->autoPowerControl = _server.hasArg("auto_power_control");

  _storage.save(_routing.get());
  redirectTo("/routing/cam?n=" + String(n));
}

// ---------------------------------------------------------------------------
// Counters
// ---------------------------------------------------------------------------

void WebConfigServer::handleCountersGet() {
  String body = "<table>";
  body += "<tr><td>RS485 RX Total</td><td>" + String(_diagnostics.rs485RxTotal()) + "</td></tr>";
  body += "<tr><td>Forwarded</td><td>" + String(_diagnostics.forwarded()) + "</td></tr>";
  body += "<tr><td>Ignored No IP</td><td>" + String(_diagnostics.ignoredNoIp()) + "</td></tr>";
  body += "<tr><td>Broadcast RX</td><td>" + String(_diagnostics.broadcastRx()) + "</td></tr>";
  body += "<tr><td>Broadcast Forwarded</td><td>" + String(_diagnostics.broadcastForwarded()) + "</td></tr>";
  body += "<tr><td>IP TX Success</td><td>" + String(_diagnostics.ipTxSuccess()) + "</td></tr>";
  body += "<tr><td>IP TX Failed</td><td>" + String(_diagnostics.ipTxFailed()) + "</td></tr>";
  body += "<tr><td>RS485 TX Response</td><td>" + String(_diagnostics.rs485TxResponse()) + "</td></tr>";
  body += "<tr><td>Malformed Packet</td><td>" + String(_diagnostics.malformedPacket()) + "</td></tr>";
  body += "<tr><td>Buffer Overflow</td><td>" + String(_diagnostics.bufferOverflow()) + "</td></tr>";
  body += "<tr><td>Packet Timeout</td><td>" + String(_diagnostics.packetTimeout()) + "</td></tr>";
  body += "<tr><td>Wi-Fi Reconnect</td><td>" + String(_diagnostics.wifiReconnect()) + "</td></tr>";
  body += "<tr><td>Uptime</td><td>" + _diagnostics.uptimeString() + "</td></tr>";
  body += "</table>";

  sendPage("Counters", body, /*refreshSeconds=*/2);
}

// ---------------------------------------------------------------------------
// Debug Mode
// ---------------------------------------------------------------------------

void WebConfigServer::handleDebugGet() {
  SystemConfig& cfg = _routing.get();
  String body = "<p>Current Debug Mode: <b>" + String(cfg.debugMode ? "ON" : "OFF") + "</b></p>";

  if (cfg.rs485Uart0Shared) {
    body += "<p style=\"color:#b8860b\">Debug ON is unavailable while RS485 UART0 Shared Mode is "
            "on - Serial IS the RS485 line in this mode, so packet-level Serial logging is "
            "disabled to avoid interfering with it. Last Packets/Live/Raw Monitor below still "
            "work (they read the in-memory log, not Serial).</p>";
    body += "<form method=\"POST\" action=\"/debug/toggle\" style=\"display:inline\">"
            "<input type=\"hidden\" name=\"to\" value=\"off\">"
            "<button type=\"submit\">Debug OFF</button></form>";
  } else {
    body += "<form method=\"POST\" action=\"/debug/toggle\" style=\"display:inline\">"
            "<input type=\"hidden\" name=\"to\" value=\"on\">"
            "<button type=\"submit\">Debug ON</button></form> ";
    body += "<form method=\"POST\" action=\"/debug/toggle\" style=\"display:inline\">"
            "<input type=\"hidden\" name=\"to\" value=\"off\">"
            "<button type=\"submit\">Debug OFF</button></form>";
  }

  body += "<h3>Last Packets</h3><pre>";
  bool any = false;
  for (uint8_t i = 0; i < _diagnostics.recentLogDepth(); i++) {
    const String& entry = _diagnostics.recentLog()[i];
    if (entry.length() == 0) continue;
    body += htmlEscape(entry) + "\n";
    any = true;
  }
  if (!any) body += "(none yet)";
  body += "</pre>";

  body += "<p><a href=\"/debug/live\">Live Packet Monitor</a> | "
          "<a href=\"/debug/raw\">Raw Byte Monitor</a></p>";

  body += "<h3>Unhandled Commands</h3>";
  body += "<p>Camera-ID-addressed commands the gateway couldn't translate or hasn't implemented "
          "yet. Recorded even while Debug Mode is OFF; repeats of the exact same command collapse "
          "into one line with a count instead of filling up the list.</p><pre>";
  uint8_t unhandledCount = _diagnostics.unhandledCount();
  if (unhandledCount == 0) {
    body += "(none yet)";
  } else {
    for (uint8_t i = 0; i < unhandledCount; i++) {
      body += htmlEscape(_diagnostics.unhandledEntry(i)) + "\n";
    }
  }
  body += "</pre>";

  body += "<h3>Send Test Command</h3>";
  body += "<p>Sends a Query Pan Position command (address 1) out on RS485, then jumps to the "
          "Raw Byte Monitor so you can see whether anything responds.</p>";
  body += "<form method=\"POST\" action=\"/debug/test-command\">";
  body += "<select name=\"protocol\"><option value=\"d\">Pelco-D</option>"
          "<option value=\"p\">Pelco-P</option></select>";
  body += "<button type=\"submit\">Send</button></form>";

  sendPage("Debug Mode", body);
}

void WebConfigServer::handleDebugTogglePost() {
  SystemConfig& cfg = _routing.get();
  bool wantsOn = (_server.arg("to") == "on");
  // UART0 Shared Mode에서는 Debug ON을 거부한다 - Serial이 곧 RS485 라인이라, 디버그
  // 프린트가 writePacket()의 DE HIGH 구간과 타이밍이 겹치면 콘솔 텍스트 일부가 RS485
  // 버스로 새 나갈 위험이 있다 (GatewayActions::setRs485Uart0SharedMode() 참고).
  if (wantsOn && cfg.rs485Uart0Shared) {
    redirectTo("/debug");
    return;
  }
  cfg.debugMode = wantsOn;
  _storage.save(cfg);
  redirectTo("/debug");
}

void WebConfigServer::handleDebugLiveGet() {
  String body = "<p>Auto-refreshing every 1s. Shows parsed RX/TX packets - enable Debug Mode on "
                "the Debug page first if this stays empty.</p><pre>";
  bool any = false;
  for (uint8_t i = 0; i < _diagnostics.recentLogDepth(); i++) {
    const String& entry = _diagnostics.recentLog()[i];
    if (entry.length() == 0) continue;
    body += htmlEscape(entry) + "\n";
    any = true;
  }
  if (!any) body += "(none yet)";
  body += "</pre>";

  sendPage("Live Packet Monitor", body, /*refreshSeconds=*/1);
}

void WebConfigServer::handleDebugRawGet() {
  String body = "<p>Auto-refreshing every 1s. Shows every RS485 byte regardless of protocol or "
                "checksum - independent of Debug Mode.</p><pre>";
  bool any = false;
  for (uint8_t i = 0; i < _diagnostics.recentRawLogDepth(); i++) {
    const String& entry = _diagnostics.recentRawLog()[i];
    if (entry.length() == 0) continue;
    body += htmlEscape(entry) + "\n";
    any = true;
  }
  if (!any) body += "(none yet)";
  body += "</pre>";

  sendPage("Raw Byte Monitor", body, /*refreshSeconds=*/1);
}

void WebConfigServer::handleDebugTestCommandPost() {
  String proto = _server.arg("protocol");
  if (proto == "d") {
    uint8_t packet[7];
    buildPelcoDTestCommand(packet);
    _rs485.writePacket(packet, sizeof(packet));
  } else if (proto == "p") {
    uint8_t packet[8];
    buildPelcoPTestCommand(packet);
    _rs485.writePacket(packet, sizeof(packet));
  }
  redirectTo("/debug/raw");
}

// ---------------------------------------------------------------------------
// Factory Reset
// ---------------------------------------------------------------------------

void WebConfigServer::handleFactoryResetGet() {
  String body = "<p style=\"color:red\"><b>WARNING:</b> This erases ALL settings (Wi-Fi, RS485, "
                "routing table, input protocol, everything) and restores factory defaults, then "
                "reboots. This cannot be undone.</p>";
  body += "<form method=\"POST\" action=\"/factory-reset\">"
          "<button type=\"submit\">Confirm Factory Reset</button></form>";

  sendPage("Factory Reset", body);
}

void WebConfigServer::handleFactoryResetPost() {
  sendPage("Factory Reset", "<p>Factory reset confirmed. Rebooting...</p>");
  performFactoryReset(_routing, _storage, _rs485, _statusLed);
}

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------

void WebConfigServer::handleNotFound() {
  _server.send(404, "text/plain", "Not Found");
}
