#pragma once

#include <Arduino.h>
#include <WebServer.h>

// ZU-EPC7000 물리 컨트롤러를 웹으로 옮긴 제어 패널 (doc/Web_controller.md).
//
// 설정 화면(WebConfigServer)과 같은 WebServer 인스턴스에 라우트만 얹는다 - 포트를 따로
// 열면 사용자가 주소를 두 개 기억해야 하고, ESP32에서 동기식 서버를 두 개 돌릴 이유도
// 없다.
//
// 이 클래스는 HTTP와 화면만 담당한다. 실제 명령 생성(어느 카메라를 IP VISCA로 보낼지
// RS485 Pelco-D로 보낼지, 버스 유휴를 언제까지 기다릴지)은 전부 main.cpp의 게이트웨이
// 로직이 하고, 여기서는 함수 포인터로 호출만 한다 - SerialMenu/WebConfigServer가
// connectWifi를 함수 포인터로 받는 것과 같은 방식이다.
class WebControl {
 public:
  // 성공하면 true. 실패 사유는 err에 담는다(웹 화면에 그대로 표시된다).
  using ExecFn = bool (*)(uint8_t cam, const String& action, int p1, int p2, String* err);
  // 화면이 1초마다 폴링하는 상태 JSON. controlAllowed는 이 요청이 제어까지 허용되는
  // 경로로 들어왔는지 - 화면이 조작 UI를 잠글지 판단하는 데 쓴다.
  using StateFn = String (*)(bool controlAllowed);

  WebControl(ExecFn exec, StateFn state) : _exec(exec), _state(state) {}

  void registerRoutes(WebServer& server);

 private:
  WebServer* _server = nullptr;
  ExecFn _exec;
  StateFn _state;

  // AP로 접속한 클라이언트에게는 제어를 허용하지 않는다 - 모니터링만 된다.
  //
  // AP는 STA 연결 여부와 무관하게 항상 떠 있고 비밀번호 하나로만 막혀 있어서, 카메라를
  // 실제로 움직이는 권한을 주기에는 문턱이 낮다. 설정 화면과 달리 제어 화면은 예배 중에
  // 누구나 폰으로 열어두게 되는 화면이라 더 그렇다. 요청이 들어온 인터페이스의 로컬
  // 주소를 AP 주소와 비교해 판별한다.
  bool controlAllowed();

  void handlePage();
  void handleState();
  void handleCmd();
};
