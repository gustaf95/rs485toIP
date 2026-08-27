#include "WebConfigServer.h"
#include <WiFi.h>
#include "BoardProfile.h"
#include "Rs485PinValidation.h"
#include "GatewayActions.h"

namespace {
// 이스케이프할 문자가 하나라도 있는지 먼저 훑는다. 실제로는 대부분 없다 - SSID나
// 카메라 이름에 &<>"가 들어가는 일은 드물다. 없으면 아래 htmlEscape()가 문자 단위
// 조립 루프를 통째로 건너뛴다(반환은 String이라 복사 한 번은 남는다).
bool needsEscaping(const String& in) {
  for (size_t i = 0; i < in.length(); i++) {
    char c = in[i];
    if (c == '&' || c == '<' || c == '>' || c == '"') return true;
  }
  return false;
}

String htmlEscape(const String& in) {
  if (!needsEscaping(in)) return in;

  String out;
  // 최악의 경우 한 글자가 6바이트(&quot;)가 되지만, 실제로 이스케이프될 문자는 극히
  // 일부다. 여유분을 조금만 잡아 재할당 한 번으로 끝나게 한다.
  out.reserve(in.length() + 16);
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

// 예전에는 <option> 하나를 String으로 만들어 반환하고 호출부가 += 했다. 한 <select>에
// 항목이 서너 개씩이고 화면당 대여섯 개의 <select>가 있어서, 그때마다 임시 String이
// 만들어졌다 버려졌다. 호출부 버퍼에 직접 이어붙이면 그 임시 객체가 전부 사라진다.
void appendOption(String& out, int value, int current, const String& label) {
  out += "<option value=\"";
  out += value;
  out += '"';
  if (value == current) out += " selected";
  out += '>';
  out += label;
  out += "</option>";
}

// 길이를 컴파일 타임에 아는 상수 리터럴을 strlen 없이 그대로 소켓으로 보낸다.
template <size_t N>
void sendLiteral(WebServer& server, const char (&literal)[N]) {
  server.sendContent(literal, N - 1);
}

// 페이지 머리말과 CSS. 매 요청마다 String에 열 번 넘게 += 해서 조립하던 것을 리터럴
// 하나로 합쳤다 - 내용이 전혀 변하지 않으므로 힙을 거칠 이유가 없다.
const char kPageHead[] =
    "<!DOCTYPE html><html><head>"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">";

const char kPageStyle[] =
    "<style>"
    "body{font-family:sans-serif;max-width:640px;margin:1em auto;padding:0 1em;line-height:1.5}"
    "nav{display:flex;flex-wrap:wrap;align-items:center;gap:0.3em 0.8em;margin-bottom:1em}"
    "nav a.here{font-weight:bold;color:#000;text-decoration:none}"
    "nav a.panel{margin-left:auto;padding:4px 12px;border-radius:4px;"
    "background:#0a72a8;color:#fff;text-decoration:none;font-weight:bold}"
    "table{border-collapse:collapse;width:100%;margin:0.5em 0}"
    "td,th{border:1px solid #ccc;padding:4px 8px;text-align:left}"
    "label{display:block;margin:0.6em 0}"
    "input,select{width:100%;max-width:320px;box-sizing:border-box;padding:4px}"
    "button,input[type=submit]{padding:6px 12px;margin-top:0.5em}"
    "pre{background:#f4f4f4;padding:0.6em;overflow-x:auto}"
    "</style></head><body>";

const uint32_t kBaudChoices[5] = {2400, 4800, 9600, 38400, 115200};

// 상단 메뉴의 설정 화면들. 제어 패널(/control)은 여기 없다 - 설정 항목이 아니라
// 다른 화면으로 넘어가는 전환이라 navHtml()에서 따로 오른쪽 끝에 붙인다.
struct NavItem {
  const char* path;
  const char* label;
};
const NavItem kNavItems[] = {
    {"/", "Status"},         {"/network", "Network"},   {"/rs485", "RS485"},
    {"/routing", "Routing"}, {"/counters", "Counters"}, {"/debug", "Debug"},
    {"/factory-reset", "Factory Reset"},
};
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
  // 제어 패널(/control, /api/*)은 같은 서버에 얹는다.
  _control.registerRoutes(_server);

  _server.onNotFound([this]() { handleNotFound(); });

  _server.begin();
}

void WebConfigServer::poll() {
  _server.handleClient();
}

// ---------------------------------------------------------------------------
// Rendering helpers
// ---------------------------------------------------------------------------

// 상단 메뉴. 지금 보고 있는 화면을 굵게 표시하고, 제어 패널은 오른쪽 끝에 버튼으로
// 떼어 놓는다.
//
// 떼어 놓은 이유는 눈에 안 띄어서다. 설정 링크 여덟 개 사이에 끼워두면 나머지와 똑같이
// 생긴 글자 한 덩어리라 "설정 항목 중 하나"로 읽히고 그냥 지나친다. /control 상단의
// Config 링크도 오른쪽 끝에 있으므로, 같은 자리에 두면 두 화면을 오가는 길이 좌우 대칭이
// 되어 왕복이 눈에 들어온다.
String WebConfigServer::navHtml() {
  String uri = _server.uri();
  String html;
  // 항목 일곱 개 + 제어 패널 버튼이 약 300바이트다. 미리 잡아두면 += 하는 동안
  // 재할당이 일어나지 않는다.
  html.reserve(384);
  html += "<nav>";
  for (const NavItem& item : kNavItems) {
    // 정확히 일치하거나, 하위 경로일 때 켠다 - /routing/cam이 Routing을,
    // /debug/live가 Debug를 가리켜야 한다. 접두사만 보면 안 되는 이유는 Status("/")다:
    // 모든 경로가 "/"로 시작해서 Status가 항상 켜져 버린다.
    bool here = (uri == item.path) || uri.startsWith(String(item.path) + "/");
    html += "<a href=\"";
    html += item.path;
    html += here ? "\" class=\"here\">" : "\">";
    html += item.label;
    html += "</a>";
  }
  html += "<a class=\"panel\" href=\"/control\">Control Panel &rarr;</a>";
  html += "</nav><hr>";
  return html;
}

// 페이지를 chunked로 흘려보낸다.
//
// 예전에는 완성된 페이지를 String 하나에 서른 번 남짓 += 해서 쌓은 뒤 send()에 넘겼다.
// 그러면 (1) String이 커지며 여러 번 재할당·복사되고, (2) 그 사본과 호출부가 넘긴
// bodyHtml이 한동안 동시에 힙에 올라간다. ESP32-C3는 WiFi/lwIP와 같은 메모리를 나눠
// 쓰는 데다 싱글코어라, 이 재할당들이 그대로 여유 힙과 loop() 시간에서 빠진다.
//
// 조각으로 보내면 페이지 전체를 담는 String 자체가 없어진다 - 변하지 않는 머리말과
// CSS는 힙을 거치지 않고 플래시에서 곧바로 소켓으로 나가고, 힙 최대 점유는 호출부가
// 넘긴 bodyHtml 하나로 줄어든다.
//
// HTTP/1.0 클라이언트에는 WebServer가 알아서 chunked를 끄고 연결 종료로 끝을 알린다
// (WebServer::_prepareHeader) - 브라우저는 전부 1.1이라 실제로는 항상 chunked다.
void WebConfigServer::sendPage(const String& title, const String& bodyHtml, uint16_t refreshSeconds) {
  _server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  _server.send(200, "text/html", "");

  sendLiteral(_server, kPageHead);
  if (refreshSeconds > 0) {
    _server.sendContent("<meta http-equiv=\"refresh\" content=\"" + String(refreshSeconds) + "\">");
  }

  // 제목은 <title>과 <h2> 두 곳에 쓰이므로 한 번만 이스케이프한다.
  const String escapedTitle = htmlEscape(title);
  _server.sendContent("<title>" + escapedTitle + "</title>");
  sendLiteral(_server, kPageStyle);
  _server.sendContent(navHtml());
  _server.sendContent("<h2>" + escapedTitle + "</h2>");
  _server.sendContent(bodyHtml);
  sendLiteral(_server, "</body></html>");

  // 길이 0인 마지막 조각이 chunked 전송의 끝을 알린다. 빠뜨리면 브라우저가 페이지를
  // 다 받고도 계속 기다린다.
  _server.sendContent("");
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

  String body;
  body.reserve(512);
  body += "<table>";
  body += "<tr><td>Mode</td><td>Gateway Running</td></tr>";
  // 어느 보드용 펌웨어가 올라가 있는지. 핀 기본값과 허용 범위가 보드마다 다르므로,
  // 설정이 이상할 때 제일 먼저 확인해야 하는 값이다.
  body += "<tr><td>Board</td><td>" BOARD_NAME " (" BOARD_RS485_UART_LABEL ")</td></tr>";
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

  // **동기 스캔을 쓰지 않는다.**
  //
  // WiFi.scanNetworks()는 기본적으로 스캔이 끝날 때까지 반환하지 않아 2~4초 동안
  // loop()를 통째로 멈춘다. 그동안 RS485 수신 링버퍼(9600bps에서 1024바이트 = 약 1초분)가
  // 넘쳐 프레임이 유실되고, 거기에 Stop 명령이 섞여 있으면 카메라가 계속 돈다 - 설정
  // 화면을 여는 것만으로 운용 중인 게이트웨이가 명령을 흘리는 셈이었다. 싱글코어인
  // ESP32-C3에서는 빠져나갈 구멍이 더 없다.
  //
  // 그래서 화면은 **직전 스캔 결과**를 그리고, 다 그린 다음 새 스캔을 비동기로 걸어둔다.
  // 처음 열면 "scanning..."이 보이고, 새로고침하면 목록이 나온다. 목록이 한 박자 늦는
  // 대신 게이트웨이가 명령을 흘리지 않는다.
  String scanOptions;
  scanOptions.reserve(640);
  scanOptions += "<option value=\"\">-- select scanned network --</option>";

  const int16_t scanState = WiFi.scanComplete();
  if (scanState > 0) {
    for (int i = 0; i < scanState; i++) {
      // 같은 SSID를 value와 표시 텍스트 두 곳에 쓰므로 한 번만 이스케이프한다.
      const String escaped = htmlEscape(WiFi.SSID(i));
      scanOptions += "<option value=\"" + escaped + "\">" + escaped + " (" +
                      String(WiFi.RSSI(i)) + "dBm)</option>";
    }
  } else if (scanState == WIFI_SCAN_RUNNING) {
    scanOptions +=
        "<option value=\"\" disabled>(scanning - reload this page in a few seconds)</option>";
  }

  // 다음 방문을 위해 새 스캔을 예약한다. async=true라 즉시 반환한다.
  //
  // 위에서 결과를 다 읽은 뒤에 부르는 것이 중요하다 - 새 스캔을 시작하면 arduino-esp32가
  // 이전 결과 배열을 해제한다. 이미 돌고 있으면 건드리지 않는다(다시 걸면 진행 중인
  // 스캔이 버려져 결과가 영영 안 나온다). scanDelete()를 부르지 않으므로 마지막 결과는
  // 다음 스캔이 시작될 때까지 남는데, AP 스무 개 남짓이면 1~2KB 수준이라 감수한다 -
  // 그게 없으면 "직전 결과를 보여준다"가 성립하지 않는다.
  if (scanState != WIFI_SCAN_RUNNING) {
    WiFi.scanNetworks(/*async=*/true);
  }

  String body = "<table>";
  body += "<tr><td>Wi-Fi Status</td><td>" + String(connected ? "Connected" : "Disconnected") + "</td></tr>";
  body += "<tr><td>Current SSID</td><td>" +
          htmlEscape(strlen(cfg.wifi.ssid) ? cfg.wifi.ssid : "(not set)") + "</td></tr>";
  body += "<tr><td>ESP32 IP (STA)</td><td>" +
          String(connected ? WiFi.localIP().toString() : "Not assigned") + "</td></tr>";
  body += "</table>";
  body += "<p><i>Status LED pin is configured under RS485 Settings. The scanned network list "
          "comes from the previous scan - reload to pick up a fresh one.</i></p>";

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
  // 이 화면이 설정 페이지 중 가장 크다(<select> 여섯 개). 미리 잡아두면 조립하는 동안
  // 재할당이 일어나지 않는다.
  body.reserve(3072);

  if (error.length() > 0) {
    body += "<p style=\"color:red\"><b>Error:</b> " + htmlEscape(error) + "</p>";
  }
  // 저장 직후 리다이렉트로 돌아올 때 붙는 경고 코드. 종류를 구분해서 보여준다 -
  // C3에서 GPIO20/21을 고르면 리셋할 때마다 부팅 로그가 RS485 버스로 나가는데,
  // 스트래핑 경고와 같은 문구로 뭉뚱그리면 그 얘기가 전달되지 않는다.
  const String warn = _server.arg("warn");
  if (warn.indexOf("bootlog") >= 0) {
    body += "<p style=\"color:#b8860b\"><b>Warning:</b> one or more pins is the ROM bootloader's "
            "log output (UART0). Every reset dumps boot messages onto that pin - keep the RS485 "
            "driver disabled at boot (pull DE/RE low) or pick another pin.</p>";
  }
  if (warn.indexOf("strap") >= 0) {
    body += "<p style=\"color:#b8860b\"><b>Warning:</b> one or more pins is a boot strapping pin - "
            "verify no external pull affects boot.</p>";
  }

  body += "<p><b>Board:</b> " BOARD_NAME " &nbsp;&middot;&nbsp; <b>RS485 UART:</b> "
          BOARD_RS485_UART_LABEL "</p>";

  body += "<form method=\"POST\" action=\"/rs485\">";

  body += "<label>Baudrate: <select name=\"baudrate\">";
  for (uint8_t i = 0; i < 5; i++) {
    appendOption(body, (int)kBaudChoices[i], (int)cfg.rs485Baudrate, String(kBaudChoices[i]));
  }
  body += "</select></label>";

  body += "<label>RX Pin: <input type=\"number\" name=\"rx_pin\" value=\"" +
          String(cfg.rs485RxPin) + "\"></label>";
  body += "<label>TX Pin: <input type=\"number\" name=\"tx_pin\" value=\"" +
          String(cfg.rs485TxPin) + "\"></label>";
  body += "<label>DE/RE Pin: <input type=\"number\" name=\"dere_pin\" value=\"" +
          String(cfg.rs485DeRePin) + "\"></label>";

  body += "<label>Signal Inversion: <select name=\"invert\">";
  appendOption(body, 1, cfg.rs485Invert ? 1 : 0, "Inverted (A/B swapped wiring)");
  appendOption(body, 0, cfg.rs485Invert ? 1 : 0, "Normal");
  body += "</select></label>";

  body += "<label>Input Protocol: <select name=\"input_protocol\">";
  appendOption(body, 0, (int)cfg.inputProtocol, "VISCA");
  appendOption(body, 1, (int)cfg.inputProtocol, "Pelco-D");
  appendOption(body, 2, (int)cfg.inputProtocol, "Pelco-P");
  appendOption(body, 3, (int)cfg.inputProtocol, "Pelco-D/P Autodetect");
  body += "</select></label>";

  body += "<label>Pelco Response Mode: <select name=\"pelco_response\">";
  appendOption(body, 0, (int)cfg.pelcoResponseMode, "Respond (synthetic ACK)");
  appendOption(body, 1, (int)cfg.pelcoResponseMode, "No response");
  body += "</select></label>";

  body += "<label>Camera Response Mode: <select name=\"response_mode\">";
  appendOption(body, 0, (int)cfg.responseMode, "none - drop camera responses");
  appendOption(body, 1, (int)cfg.responseMode, "synthetic - fake ACK/Completion (VISCA input only)");
  appendOption(body, 2, (int)cfg.responseMode, "forward - camera bytes to RS485 as-is");
  appendOption(body, 3, (int)cfg.responseMode, "forward_rewrite - forward, rewrite address to 0x9n");
  body += "</select></label>";
  body += "<p><i>forward/forward_rewrite send RAW VISCA bytes. A Pelco-D/P controller cannot "
          "parse those - repackaging into a Pelco response is not implemented yet.</i></p>";

  body += "<label>Status LED Pin: <input type=\"number\" name=\"led_pin\" value=\"" +
          String(cfg.statusLedPin) + "\"></label>";

  body += "<label>Status LED Logic: <select name=\"led_active_low\">";
  appendOption(body, 0, cfg.statusLedActiveLow ? 1 : 0, "Active High (HIGH = on)");
  appendOption(body, 1, cfg.statusLedActiveLow ? 1 : 0,
               "Active Low (LOW = on) - ESP32-C3 Super Mini onboard LED (GPIO8)");
  body += "</select></label>";

  body += "<button type=\"submit\">Save</button></form>";

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

  int rx = _server.arg("rx_pin").toInt();
  int tx = _server.arg("tx_pin").toInt();
  int dere = _server.arg("dere_pin").toInt();

  if (!validateRs485Pin(rx, /*requireOutput=*/false, cfg.statusLedPin, &error) ||
      !validateRs485Pin(tx, /*requireOutput=*/true, cfg.statusLedPin, &error) ||
      !validateRs485Pin(dere, /*requireOutput=*/true, cfg.statusLedPin, &error)) {
    sendPage("RS485 Settings", rs485PageBody(error));
    return;
  }

  const uint8_t warnFlags = gpioWarningFlags((uint8_t)rx, (uint8_t)tx, (uint8_t)dere);

  cfg.rs485RxPin = (uint8_t)rx;
  cfg.rs485TxPin = (uint8_t)tx;
  cfg.rs485DeRePin = (uint8_t)dere;

  cfg.rs485Baudrate = (uint32_t)_server.arg("baudrate").toInt();
  cfg.rs485Invert = (_server.arg("invert").toInt() != 0);
  // 폼에 없는 값이 POST로 들어와도(직접 조작하거나 옛 페이지가 캐시된 경우) 삭제된
  // Raw Bridge(4)로 넘어가지 않도록 범위를 확인하고, 벗어나면 그냥 무시한다.
  int inputProtocol = _server.arg("input_protocol").toInt();
  if (inputProtocol >= 0 && inputProtocol <= (int)InputProtocol::PELCO_AUTO) {
    cfg.inputProtocol = (InputProtocol)inputProtocol;
  }
  cfg.pelcoResponseMode = (PelcoResponseMode)_server.arg("pelco_response").toInt();
  cfg.responseMode = (ResponseMode)_server.arg("response_mode").toInt();
  cfg.statusLedPin = (uint8_t)ledPin;
  cfg.statusLedActiveLow = (_server.arg("led_active_low").toInt() != 0);

  _storage.save(cfg);
  _rs485.begin(cfg.rs485Baudrate, cfg.rs485RxPin, cfg.rs485TxPin, cfg.rs485DeRePin,
               cfg.rs485Invert);
  _statusLed.begin(cfg.statusLedPin, cfg.statusLedActiveLow);

  String target = "/rs485";
  if (warnFlags != GPIO_WARN_NONE) {
    target += "?warn=";
    if (warnFlags & GPIO_WARN_BOOT_LOG) target += "bootlog";
    if (warnFlags == (GPIO_WARN_BOOT_LOG | GPIO_WARN_STRAPPING)) target += "+";
    if (warnFlags & GPIO_WARN_STRAPPING) target += "strap";
  }
  redirectTo(target);
}

// ---------------------------------------------------------------------------
// Routing Table
// ---------------------------------------------------------------------------

void WebConfigServer::handleRoutingGet() {
  static const char* kProtocolNames[] = {"IP_VISCA_RAW_UDP", "IP_VISCA_RAW_TCP", "SONY_VISCA_UDP"};
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
  appendOption(body, 0, (int)slot->protocol, "IP_VISCA_RAW_UDP");
  appendOption(body, 1, (int)slot->protocol, "IP_VISCA_RAW_TCP");
  appendOption(body, 2, (int)slot->protocol, "SONY_VISCA_UDP");
  body += "</select></label>";

  body += "<label>Address Mode: <select name=\"address_mode\">";
  appendOption(body, 0, (int)slot->addressMode, "rewrite_0x81");
  appendOption(body, 1, (int)slot->addressMode, "preserve");
  appendOption(body, 2, (int)slot->addressMode, "rewrite_by_cam");
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

  // 삭제된 RAW_DATA_UDP(3)가 POST로 다시 들어오지 않도록 범위를 확인한다
  // (handleRs485Post()의 input_protocol과 같은 이유).
  int protocol = _server.arg("protocol").toInt();
  if (protocol >= 0 && protocol <= (int)ProtocolMode::SONY_VISCA_UDP) {
    slot->protocol = (ProtocolMode)protocol;
  }
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
  body += "<tr><td>Web RS485 TX</td><td>" + String(_diagnostics.webTx()) + "</td></tr>";
  body += "<tr><td>Web RS485 TX Dropped</td><td>" + String(_diagnostics.webTxDropped()) + "</td></tr>";
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

  body += "<form method=\"POST\" action=\"/debug/toggle\" style=\"display:inline\">"
          "<input type=\"hidden\" name=\"to\" value=\"on\">"
          "<button type=\"submit\">Debug ON</button></form> ";
  body += "<form method=\"POST\" action=\"/debug/toggle\" style=\"display:inline\">"
          "<input type=\"hidden\" name=\"to\" value=\"off\">"
          "<button type=\"submit\">Debug OFF</button></form>";

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
  cfg.debugMode = (_server.arg("to") == "on");
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
