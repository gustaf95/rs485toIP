#pragma once

#include <Arduino.h>
#include "RoutingTable.h"
#include "Storage.h"
#include "Diagnostics.h"
#include "Rs485Port.h"

// USB Serial(UART0) 기반 설정/진단 메뉴. UART0가 RS485와 공유되므로 poll()로 직접
// 읽지 않고, 호출자(main.cpp)가 Serial에서 읽은 바이트를 feedByte()로 하나씩
// 넘겨준다 - 같은 바이트를 VISCA 파서에도 동시에 넘길 수 있게 하기 위함이다.
class SerialMenu {
 public:
  using WifiRetryFn = void (*)();

  SerialMenu(RoutingTable& routing, Storage& storage, Diagnostics& diagnostics, Rs485Port& rs485,
             WifiRetryFn wifiRetry)
      : _routing(routing),
        _storage(storage),
        _diagnostics(diagnostics),
        _rs485(rs485),
        _wifiRetry(wifiRetry) {}

  void begin();
  void feedByte(char c);

  // 부팅/RS485 트래픽으로 인한 불필요한 출력을 막기 위해, 사용자가 Enter를 한 번
  // 입력하기 전까지는 메뉴를 아무것도 출력하지 않는다. Enter가 처음 입력되면 그
  // 시점부터 활성화되어 Main Menu를 보여준다.
  bool isActive() const { return _started; }

 private:
  enum class Screen {
    MAIN,
    NETWORK,
    IP_MODE,
    STATIC_IP,
    RS485,
    ROUTING,
    CAMERA_DETAIL,
    COUNTERS
  };
  enum class Prompt {
    NONE,
    WIFI_SSID,
    WIFI_SSID_CHOICE,
    WIFI_PASSWORD,
    STATIC_IP_VALUE,
    STATIC_GATEWAY_VALUE,
    STATIC_SUBNET_VALUE,
    RS485_BAUD_CHOICE,
    ROUTING_SELECT_CAM,
    ROUTING_SET_IP_VALUE,
    ROUTING_SET_PORT_VALUE,
    ROUTING_SET_PROTOCOL_VALUE,
    ROUTING_SET_ADDRMODE_VALUE
  };

  RoutingTable& _routing;
  Storage& _storage;
  Diagnostics& _diagnostics;
  Rs485Port& _rs485;
  WifiRetryFn _wifiRetry;

  Screen _screen = Screen::MAIN;
  Prompt _prompt = Prompt::NONE;
  uint8_t _pendingCamNumber = 0;
  uint8_t _scanCount = 0;
  bool _started = false;

  String _lineBuffer;

  void handleLine(const String& line);
  void handleMainMenu(const String& line);
  void handleNetworkMenu(const String& line);
  void handleIpModeMenu(const String& line);
  void handleStaticIpMenu(const String& line);
  void handleRs485Menu(const String& line);
  void handleRoutingMenu(const String& line);
  void handleCameraDetailMenu(const String& line);
  void handleCountersMenu(const String& line);
  void handlePrompt(const String& line);

  // RS485 Baudrate 변경 시 flash에 저장하고 즉시 UART를 재적용한다. RX/TX/DE-RE
  // 핀은 이 보드에서 고정 결선이라 메뉴에서 바꿀 수 없다.
  void applyRs485Settings(const SystemConfig& cfg);

  void printMainMenu();
  void printNetworkMenu();
  void printIpModeMenu();
  void printStaticIpMenu();
  void printRs485Menu();
  void printRoutingMenu();
  void printCameraDetailMenu();
  void printCountersMenu();

  static String protocolName(ProtocolMode mode);
  static String addressModeName(AddressMode mode);
};
