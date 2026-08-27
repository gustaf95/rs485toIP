#pragma once

#include <Arduino.h>
#include "RoutingTable.h"
#include "Storage.h"
#include "Diagnostics.h"
#include "Rs485Port.h"
#include "StatusLed.h"

// USB Serial(UART0) 기반 설정/진단 메뉴. poll()은 loop()에서 매 회전마다 호출되어야
// 하며, 한 줄 입력이 완성되기 전까지는 즉시 반환하므로 RS485 수신/IP 전송을 막지 않는다.
class SerialMenu {
 public:
  using WifiRetryFn = void (*)();

  SerialMenu(RoutingTable& routing, Storage& storage, Diagnostics& diagnostics, Rs485Port& rs485,
             StatusLed& statusLed, WifiRetryFn wifiRetry)
      : _routing(routing),
        _storage(storage),
        _diagnostics(diagnostics),
        _rs485(rs485),
        _statusLed(statusLed),
        _wifiRetry(wifiRetry) {}

  void begin();
  void poll();

  // main.cpp의 RS485 읽기 루프가 매 바이트마다 확인한다 - true면 파싱 결과와
  // 무관하게 그 바이트를 hex로 그대로 echo해야 한다 (Raw Byte Monitor 화면).
  bool rawMonitorActive() const { return _screen == Screen::DEBUG_RAW; }

  // 메뉴가 잠금 해제됐는지(빈 줄 Enter 두 번) - main.cpp가 부팅/Wi-Fi 상태 메시지를
  // 잠금 해제 전까지 완전히 침묵시키는 데 쓴다 (리셋 직후 Serial에 아무 것도 안
  // 찍히게 하기 위함). Debug Mode ON/OFF로 나오는 RX/TX 로그는 별개다 - 그건 사용자가
  // (Serial이든 Web이든) 명시적으로 켠 기능이라 이 잠금과 무관하게 항상 동작한다.
  bool menuActive() const { return _menuActive; }

 private:
  enum class Screen {
    MAIN,
    NETWORK,
    IP_MODE,
    STATIC_IP,
    RS485,
    ROUTING,
    CAMERA_DETAIL,
    COUNTERS,
    DEBUG,
    DEBUG_LIVE,
    DEBUG_RAW
  };
  enum class Prompt {
    NONE,
    WIFI_SSID,
    WIFI_SSID_CHOICE,
    WIFI_PASSWORD,
    AP_SSID,
    AP_PASSWORD,
    STATIC_IP_VALUE,
    STATIC_GATEWAY_VALUE,
    STATIC_SUBNET_VALUE,
    RS485_BAUD_CHOICE,
    RS485_RX_PIN,
    RS485_TX_PIN,
    RS485_DERE_PIN,
    RS485_INPUT_PROTOCOL_CHOICE,
    RS485_PELCO_RESPONSE_CHOICE,
    RS485_STATUS_LED_PIN,
    RS485_LED_POLARITY_CHOICE,
    RS485_INVERT_CHOICE,
    RESPONSE_MODE_CHOICE,
    ROUTING_SELECT_CAM,
    ROUTING_SET_IP_VALUE,
    ROUTING_SET_PORT_VALUE,
    ROUTING_SET_PROTOCOL_VALUE,
    ROUTING_SET_ADDRMODE_VALUE,
    FACTORY_RESET_CONFIRM,
    DEBUG_TEST_CMD_PROTOCOL_CHOICE
  };

  RoutingTable& _routing;
  Storage& _storage;
  Diagnostics& _diagnostics;
  Rs485Port& _rs485;
  StatusLed& _statusLed;
  WifiRetryFn _wifiRetry;

  Screen _screen = Screen::MAIN;
  Prompt _prompt = Prompt::NONE;
  uint8_t _pendingCamNumber = 0;
  uint8_t _scanCount = 0;

  String _lineBuffer;
  char _lastLineEndChar = 0;  // CRLF/LFCR 쌍의 두 번째 바이트를 중복 처리하지 않기 위한 상태
  bool _debugModeBeforeLive = false;  // Live Packet Monitor 진입 전 debugMode 값 - 나갈 때 복원

  // 부팅/리셋 직후에는 메뉴가 잠겨 있다 - 빈 줄로 Enter를 연속 두 번 눌러야 열린다.
  // 리셋할 때마다 자동으로 Main Menu가 튀어나오지 않게 하기 위함.
  bool _menuActive = false;
  uint8_t _wakeupEnterCount = 0;

  void handleLine(const String& line);
  void handleMainMenu(const String& line);
  void handleNetworkMenu(const String& line);
  void handleIpModeMenu(const String& line);
  void handleStaticIpMenu(const String& line);
  void handleRs485Menu(const String& line);
  void handleRoutingMenu(const String& line);
  void handleCameraDetailMenu(const String& line);
  void handleCountersMenu(const String& line);
  void handleDebugMenu(const String& line);
  void handleDebugLiveMenu(const String& line);
  void handleDebugRawMenu(const String& line);
  void handlePrompt(const String& line);

  // RS485 설정(Baudrate/RX/TX/DE-RE Pin) 변경 시 flash에 저장하고 즉시 UART를 재적용한다.
  void applyRs485Settings(const SystemConfig& cfg);

  // 핀 입력 프롬프트(RX/TX/DE-RE/Status LED)의 공통 처리. 자세한 계약은 .cpp 주석 참고.
  bool readPinPrompt(const String& line, Prompt retryPrompt, bool isStatusLed,
                     bool requireOutput, uint8_t* pinOut);
  // 거부 대상은 아니지만 알려야 하는 핀에 대한 경고를 찍는다 (없으면 아무것도 안 한다).
  void printPinWarning(uint8_t pin);

  void printMainMenu();
  void printNetworkMenu();
  void printIpModeMenu();
  void printStaticIpMenu();
  void printRs485Menu();
  void printRoutingMenu();
  void printCameraDetailMenu();
  void printCountersMenu();
  void printDebugMenu();
  void printDebugLiveMenu();
  void printDebugRawMenu();

  static String protocolName(ProtocolMode mode);
  static String addressModeName(AddressMode mode);
  static String inputProtocolName(InputProtocol mode);
  static String pelcoResponseModeName(PelcoResponseMode mode);
  static String responseModeName(ResponseMode mode);
};
