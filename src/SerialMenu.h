#pragma once

#include <Arduino.h>
#include "RoutingTable.h"
#include "Storage.h"
#include "Diagnostics.h"
#include "Rs485Port.h"

// USB Serial(UART0) 기반 설정/진단 메뉴. poll()은 loop()에서 매 회전마다 호출되어야
// 하며, 한 줄 입력이 완성되기 전까지는 즉시 반환하므로 RS485 수신/IP 전송을 막지 않는다.
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
  void poll();

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
    DEBUG_LIVE
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
    RS485_RX_PIN,
    RS485_TX_PIN,
    RS485_DERE_PIN,
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

  String _lineBuffer;
  char _lastLineEndChar = 0;  // CRLF/LFCR 쌍의 두 번째 바이트를 중복 처리하지 않기 위한 상태

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
  void handlePrompt(const String& line);

  // RS485 설정(Baudrate/RX/TX/DE-RE Pin) 변경 시 flash에 저장하고 즉시 UART를 재적용한다.
  void applyRs485Settings(const SystemConfig& cfg);

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

  static String protocolName(ProtocolMode mode);
  static String addressModeName(AddressMode mode);
};
