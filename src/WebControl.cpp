#include "WebControl.h"
#include <WiFi.h>
#include "generated/WebAssets.h"

// 제어 패널 페이지는 **web/control.html**에 있다. 빌드할 때 tools/embed_web.py가 그 파일을
// gzip으로 압축해 위 헤더의 PROGMEM 배열(CONTROL_HTML_GZ)로 굽고, 여기서는 그대로
// 내보내기만 한다.
//
// 파일로 뺀 이유는 편집 때문이다 - C++ 문자열 안에 있으면 문법 강조도, 포매터도, 브라우저
// 미리보기도 못 쓴다. 그렇다고 LittleFS로 두면 펌웨어와 데이터를 따로 업로드해야 해서 한쪽만
// 올렸을 때 화면이 조용히 낡은 채로 남는다. 빌드 시 굽는 방식이 둘 다 피한다
// (web/README.md 참고).

void WebControl::registerRoutes(WebServer& server) {
  _server = &server;
  server.on("/control", HTTP_GET, [this]() { handlePage(); });
  server.on("/api/state", HTTP_GET, [this]() { handleState(); });
  server.on("/api/cmd", HTTP_POST, [this]() { handleCmd(); });
}

bool WebControl::controlAllowed() {
  IPAddress local = _server->client().localIP();
  IPAddress ap = WiFi.softAPIP();

  if (local == ap) return false;  // AP 인터페이스로 들어온 요청

  // localIP()를 못 읽는 경우(소켓 상태에 따라 0.0.0.0이 나올 수 있다)에는 클라이언트
  // 주소가 AP 서브넷인지로 판단한다. AP IP는 ESP32 기본값 192.168.4.1 고정이다.
  if (local == IPAddress(0, 0, 0, 0)) {
    IPAddress remote = _server->client().remoteIP();
    if (remote[0] == ap[0] && remote[1] == ap[1] && remote[2] == ap[2]) return false;
  }
  return true;
}

void WebControl::handlePage() {
  // 페이지 자체는 AP에서도 열린다 - 제어만 막히고 상태 표시는 그대로 쓸 수 있다.
  // 조작 UI 잠금은 /api/state의 control 플래그를 보고 화면이 스스로 한다.
  //
  // gzip으로 구워져 있으므로 헤더를 붙여 그대로 내보낸다. 브라우저는 전부 이 인코딩을
  // 받아들이고, 압축 해제는 브라우저가 한다.
  _server->sendHeader("Content-Encoding", "gzip");
  _server->send_P(200, "text/html", (PGM_P)CONTROL_HTML_GZ, CONTROL_HTML_GZ_LEN);
}

void WebControl::handleState() {
  _server->send(200, "application/json", _state(controlAllowed()));
}

void WebControl::handleCmd() {
  if (!controlAllowed()) {
    _server->send(403, "application/json",
                  "{\"ok\":false,\"err\":\"control is blocked on the AP - use the STA address\"}");
    return;
  }

  uint8_t cam = (uint8_t)_server->arg("cam").toInt();
  String action = _server->arg("action");
  int p1 = _server->arg("p1").toInt();
  int p2 = _server->arg("p2").toInt();

  String err;
  bool ok = _exec(cam, action, p1, p2, &err);
  if (ok) {
    _server->send(200, "application/json", "{\"ok\":true}");
  } else {
    err.replace("\"", "'");
    _server->send(200, "application/json", "{\"ok\":false,\"err\":\"" + err + "\"}");
  }
}
