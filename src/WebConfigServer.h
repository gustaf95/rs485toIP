#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include "RoutingTable.h"
#include "Storage.h"
#include "Diagnostics.h"
#include "Rs485Port.h"
#include "StatusLed.h"
#include "WebControl.h"

// USB Serial(SerialMenu)의 웹 버전. USB를 물리적으로 꽂을 수 없는 이미 설치된
// 장비나, UART0가 RS485와 충돌해서 Serial 메뉴를 쓸 수 없는 보드를 위한 두 번째
// 설정 인터페이스다. SerialMenu와 동일한 RoutingTable/Storage/Diagnostics를
// 그대로 참조하므로 두 UI가 항상 같은 flash 설정을 보고 쓴다.
//
// AP는 STA(평소 WiFi)와 무관하게 항상 띄운다 - WiFi가 아예 연결 안 된 상태에서도
// 접근 경로를 보장하기 위함이다. 인증은 없다(신뢰된 LAN/AP 전제) - AP 자체의
// WiFi 접속 비밀번호(config.h AP_PASSWORD_DEFAULT)만 최소한의 방어선이다.
class WebConfigServer {
 public:
  using WifiRetryFn = void (*)();

  // control은 같은 WebServer에 제어 패널(/control, /api/*) 라우트를 얹는다 - 포트를
  // 따로 열지 않으므로 사용자는 주소 하나만 기억하면 된다 (WebControl.h 참고).
  WebConfigServer(RoutingTable& routing, Storage& storage, Diagnostics& diagnostics,
                   Rs485Port& rs485, StatusLed& statusLed, WifiRetryFn wifiRetry,
                   WebControl& control)
      : _server(WEB_SERVER_PORT),
        _routing(routing),
        _storage(storage),
        _diagnostics(diagnostics),
        _rs485(rs485),
        _statusLed(statusLed),
        _wifiRetry(wifiRetry),
        _control(control) {}

  // AP를 띄우고 라우트를 등록한 뒤 서버를 시작한다. setup()에서 한 번 호출.
  void begin();

  // 동기식 WebServer라 매 loop() 회전마다 호출해야 요청을 처리한다.
  void poll();

 private:
  WebServer _server;
  RoutingTable& _routing;
  Storage& _storage;
  Diagnostics& _diagnostics;
  Rs485Port& _rs485;
  StatusLed& _statusLed;
  WifiRetryFn _wifiRetry;
  WebControl& _control;

  // ---- 페이지 핸들러 ----
  void handleStatus();
  void handleNetworkGet();
  void handleNetworkPost();
  void handleNetworkApPost();
  void handleNetworkRetry();
  void handleRs485Get();
  void handleRs485Post();
  void handleRoutingGet();
  void handleRoutingCamGet();
  void handleRoutingCamPost();
  void handleCountersGet();
  void handleDebugGet();
  void handleDebugTogglePost();
  void handleDebugLiveGet();
  void handleDebugRawGet();
  void handleDebugTestCommandPost();
  void handleFactoryResetGet();
  void handleFactoryResetPost();

  // ---- 펌웨어 업데이트 (OTA) ----
  // 업로드는 WebServer가 두 콜백으로 나눠 부른다. handleUpdateUpload()가 조각마다
  // 불려 flash에 쓰고, 다 받은 뒤에 handleUpdateDone()이 한 번 불려 응답을 낸다.
  // **업로드 콜백에서는 응답을 보낼 수 없다** - 그래서 거기서 만난 실패 사유를
  // _updateError에 담아 두고 Done 쪽에서 화면으로 낸다.
  void handleUpdateGet();
  void handleUpdateUpload();
  void handleUpdateDone();

  void handleNotFound();

  // ---- 렌더링 도우미 ----
  String rs485PageBody(const String& error);

  // 업로드 중 감지한 실패 사유. 비어 있으면 성공이다.
  String _updateError;
  // 이미지 헤더(칩 종류)를 확인했는지. 첫 조각에서 한 번만 본다.
  bool _updateHeaderChecked = false;
  // 상단 메뉴. 현재 URI(_server.uri())를 보고 지금 화면을 표시한다.
  String navHtml();
  // refreshSeconds > 0이면 <meta http-equiv="refresh">를 넣어 폴링 화면(Counters,
  // Live/Raw Monitor)을 만든다. 0이면 일반 페이지.
  void sendPage(const String& title, const String& bodyHtml, uint16_t refreshSeconds = 0);
  void redirectTo(const String& path);
};
