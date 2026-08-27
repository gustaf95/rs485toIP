#include "SerialMenu.h"
#include <WiFi.h>
#include "BoardProfile.h"
#include "Rs485PinValidation.h"
#include "GatewayActions.h"

namespace {
const uint32_t kBaudChoices[5] = {2400, 4800, 9600, 38400, 115200};
}  // namespace

String SerialMenu::protocolName(ProtocolMode mode) {
  switch (mode) {
    case ProtocolMode::IP_VISCA_RAW_UDP: return "IP_VISCA_RAW_UDP";
    case ProtocolMode::IP_VISCA_RAW_TCP: return "IP_VISCA_RAW_TCP";
    case ProtocolMode::SONY_VISCA_UDP: return "SONY_VISCA_UDP";
  }
  return "?";
}

String SerialMenu::addressModeName(AddressMode mode) {
  switch (mode) {
    case AddressMode::REWRITE_0x81: return "rewrite_0x81";
    case AddressMode::PRESERVE: return "preserve";
    case AddressMode::REWRITE_BY_CAM: return "rewrite_by_cam";
  }
  return "?";
}

String SerialMenu::inputProtocolName(InputProtocol mode) {
  switch (mode) {
    case InputProtocol::VISCA: return "VISCA";
    case InputProtocol::PELCO_D: return "Pelco-D";
    case InputProtocol::PELCO_P: return "Pelco-P";
    case InputProtocol::PELCO_AUTO: return "Pelco-D/P Autodetect";
  }
  return "?";
}

String SerialMenu::pelcoResponseModeName(PelcoResponseMode mode) {
  switch (mode) {
    case PelcoResponseMode::SYNTHETIC: return "Respond (synthetic ACK)";
    case PelcoResponseMode::NONE: return "No response";
  }
  return "?";
}

String SerialMenu::responseModeName(ResponseMode mode) {
  switch (mode) {
    case ResponseMode::NONE: return "none (drop camera responses)";
    case ResponseMode::SYNTHETIC: return "synthetic (VISCA input only)";
    case ResponseMode::FORWARD: return "forward (raw camera bytes)";
    case ResponseMode::FORWARD_REWRITE: return "forward_rewrite (rewrite address)";
  }
  return "?";
}

// 의도적으로 아무것도 출력하지 않는다 - 리셋 직후 Serial은 완전히 침묵해야 하고
// (main.cpp의 부팅/Wi-Fi 메시지도 마찬가지, menuActive() 참고), 메뉴를 여는 방법은
// 문서(readme.md)에만 남긴다.
void SerialMenu::begin() {}

void SerialMenu::poll() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n') {
      // MobaXterm 등은 Enter로 '\r'만 보내고, 다른 터미널은 '\n' 또는 '\r\n'을 보낸다.
      // 두 문자 모두 줄 종료로 인식하되, '\r\n'/'\n\r' 쌍의 두 번째 바이트는 같은
      // Enter 입력이 중복 접수되지 않도록 건너뛴다.
      if (_lastLineEndChar != 0 && c != _lastLineEndChar) {
        _lastLineEndChar = 0;
        continue;
      }
      _lastLineEndChar = c;
      Serial.println();
      String line = _lineBuffer;
      line.trim();
      _lineBuffer = "";

      if (!_menuActive) {
        // 메뉴가 잠긴 상태 - 아무것도 안 치고 Enter만 두 번 연속 눌러야 열린다.
        // 뭔가 타이핑하고 Enter를 치면(라인이 비어있지 않으면) 카운트를 리셋한다 -
        // 노이즈나 의도치 않은 입력으로 메뉴가 갑자기 열리는 걸 막기 위함.
        if (line.length() == 0) {
          _wakeupEnterCount++;
          if (_wakeupEnterCount >= 2) {
            _menuActive = true;
            _wakeupEnterCount = 0;
            printMainMenu();
          }
        } else {
          _wakeupEnterCount = 0;
        }
        continue;
      }

      handleLine(line);
    } else if (c == 0x08 || c == 0x7F) {  // Backspace(BS) 또는 Delete(DEL)
      _lastLineEndChar = 0;
      if (_lineBuffer.length() > 0) {
        _lineBuffer.remove(_lineBuffer.length() - 1);
        Serial.print("\b \b");  // 커서를 뒤로, 문자를 공백으로 지우고, 다시 뒤로
      }
    } else {
      _lastLineEndChar = 0;
      Serial.write(c);
      _lineBuffer += c;
    }
  }
}

void SerialMenu::handleLine(const String& line) {
  if (_prompt != Prompt::NONE) {
    handlePrompt(line);
    return;
  }

  switch (_screen) {
    case Screen::MAIN: handleMainMenu(line); break;
    case Screen::NETWORK: handleNetworkMenu(line); break;
    case Screen::IP_MODE: handleIpModeMenu(line); break;
    case Screen::STATIC_IP: handleStaticIpMenu(line); break;
    case Screen::RS485: handleRs485Menu(line); break;
    case Screen::ROUTING: handleRoutingMenu(line); break;
    case Screen::CAMERA_DETAIL: handleCameraDetailMenu(line); break;
    case Screen::COUNTERS: handleCountersMenu(line); break;
    case Screen::DEBUG: handleDebugMenu(line); break;
    case Screen::DEBUG_LIVE: handleDebugLiveMenu(line); break;
    case Screen::DEBUG_RAW: handleDebugRawMenu(line); break;
  }
}

// ---------------------------------------------------------------------------
// Main menu
// ---------------------------------------------------------------------------

void SerialMenu::printMainMenu() {
  SystemConfig& cfg = _routing.get();
  bool connected = WiFi.status() == WL_CONNECTED;

  Serial.println();
  Serial.println("============================================================");
  Serial.println(" ESP32 RS485 VISCA to IP VISCA Gateway");
  Serial.println(" Firmware : v2.0.0");
  Serial.println("============================================================");
  Serial.println();
  Serial.println("[STATUS]");
  Serial.println("  Mode             : Gateway Running");
  Serial.print("  Wi-Fi            : ");
  Serial.println(connected ? "Connected" : "Disconnected");
  Serial.print("  ESP32 IP         : ");
  Serial.println(connected ? WiFi.localIP().toString() : "Not assigned");
  Serial.print("  Debug Mode       : ");
  Serial.println(cfg.debugMode ? "ON" : "OFF");
  Serial.println();
  Serial.println("------------------------------------------------------------");
  Serial.println(" Main Menu");
  Serial.println("------------------------------------------------------------");
  Serial.println("  1. Network Settings");
  Serial.println("  2. RS485 Settings");
  Serial.println("  3. Routing Table");
  Serial.println("  4. Counters");
  Serial.println("  5. Debug Mode");
  Serial.println("  6. Factory Reset");
  Serial.println();
  Serial.println("============================================================");
  Serial.print("Select menu number: ");
}

void SerialMenu::handleMainMenu(const String& line) {
  if (line == "1") {
    _screen = Screen::NETWORK;
    printNetworkMenu();
  } else if (line == "2") {
    _screen = Screen::RS485;
    printRs485Menu();
  } else if (line == "3") {
    _screen = Screen::ROUTING;
    printRoutingMenu();
  } else if (line == "4") {
    _screen = Screen::COUNTERS;
    printCountersMenu();
  } else if (line == "5") {
    _screen = Screen::DEBUG;
    printDebugMenu();
  } else if (line == "6") {
    Serial.println();
    Serial.println("WARNING: This erases ALL settings (Wi-Fi, RS485, routing table,");
    Serial.println("input protocol, everything) and restores factory defaults, then");
    Serial.println("reboots. This cannot be undone.");
    Serial.print("Type YES to confirm, or press Enter to cancel: ");
    _prompt = Prompt::FACTORY_RESET_CONFIRM;
  } else {
    printMainMenu();
  }
}

// ---------------------------------------------------------------------------
// Network Settings
// ---------------------------------------------------------------------------

void SerialMenu::printNetworkMenu() {
  SystemConfig& cfg = _routing.get();
  bool connected = WiFi.status() == WL_CONNECTED;

  Serial.println();
  Serial.println("============================================================");
  Serial.println(" 1. Network Settings");
  Serial.println("============================================================");
  Serial.println();
  Serial.print("  Wi-Fi Mode       : AP+STA\n");
  Serial.print("  Wi-Fi Status     : ");
  Serial.println(connected ? "Connected" : "Disconnected");
  Serial.print("  SSID             : ");
  Serial.println(strlen(cfg.wifi.ssid) ? cfg.wifi.ssid : "(not set)");
  Serial.print("  DHCP             : ");
  Serial.println(cfg.wifi.useDhcp ? "Enabled" : "Disabled");
  Serial.print("  ESP32 IP         : ");
  Serial.println(connected ? WiFi.localIP().toString() : "Not assigned");
  Serial.print("  AP SSID          : ");
  Serial.println(WiFi.softAPSSID());
  Serial.print("  AP IP            : ");
  Serial.println(WiFi.softAPIP());
  Serial.print("  Gateway          : ");
  Serial.println(cfg.wifi.gateway.toIPAddress().toString());
  Serial.print("  Subnet           : ");
  Serial.println(cfg.wifi.subnet.toIPAddress().toString());
  Serial.println();
  Serial.println("  (Status LED pin is configured under RS485 Settings.)");
  Serial.println();
  Serial.println("------------------------------------------------------------");
  Serial.println(" Options");
  Serial.println("------------------------------------------------------------");
  Serial.println("  1. Set Wi-Fi SSID");
  Serial.println("  2. Set Wi-Fi Password");
  Serial.println("  3. Set DHCP / Static IP");
  Serial.println("  4. Retry Wi-Fi Connection");
  Serial.println("  5. Set AP SSID");
  Serial.println("  6. Set AP Password");
  Serial.println("  0. Back to Main Menu");
  Serial.print("> ");
}

void SerialMenu::handleNetworkMenu(const String& line) {
  if (line == "1") {
    // 여기만 동기 스캔을 그대로 둔다. 스캔이 끝날 때까지 2~4초 loop()가 멈추고 그동안
    // RS485 수신 링버퍼가 넘칠 수 있는데(웹 설정 화면에서는 그래서 비동기로 바꿨다,
    // WebConfigServer::handleNetworkGet 참고), 이 화면은 성격이 다르다 - 결과를 번호로
    // 매겨 바로 다음 입력에서 고르게 하는 구조라 비동기로 바꾸면 "스캔 걸기"와
    // "목록 보기"가 두 단계로 갈라진다. 콘솔 앞에 사람이 서서 설정하는 중이라는 것도
    // 웹과 다르다 - 예배 중에 누가 폰으로 열 수 있는 화면이 아니다.
    //
    // 대신 버스가 잠깐 먹통이 된다는 사실은 숨기지 않고 알린다.
    Serial.println("Scanning Wi-Fi networks... (RS485 input is not processed for a few seconds)");
    int found = WiFi.scanNetworks();
    _scanCount = (found > 0) ? (found > 30 ? 30 : (uint8_t)found) : 0;

    Serial.println("   1. Enter SSID manually");
    if (_scanCount == 0) {
      Serial.println("No networks found.");
    } else {
      for (uint8_t i = 0; i < _scanCount; i++) {
        char entry[80];
        snprintf(entry, sizeof(entry), "  %2u. %-32s (%d dBm)%s", i + 2, WiFi.SSID(i).c_str(),
                 WiFi.RSSI(i), WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? " [open]" : "");
        Serial.println(entry);
      }
    }
    Serial.println("   0. Back to Main Menu");
    Serial.print("Select network number: ");
    _prompt = Prompt::WIFI_SSID_CHOICE;
  } else if (line == "2") {
    Serial.print("Enter Wi-Fi Password: ");
    _prompt = Prompt::WIFI_PASSWORD;
  } else if (line == "3") {
    _screen = Screen::IP_MODE;
    printIpModeMenu();
  } else if (line == "4") {
    Serial.println("Retrying Wi-Fi connection...");
    if (_wifiRetry) _wifiRetry();
    printNetworkMenu();
  } else if (line == "5") {
    Serial.print("Enter AP SSID (blank to cancel, currently \"");
    Serial.print(WiFi.softAPSSID());
    Serial.print("\"): ");
    _prompt = Prompt::AP_SSID;
  } else if (line == "6") {
    Serial.print("Enter AP Password (blank to cancel, min 8 chars for WPA2): ");
    _prompt = Prompt::AP_PASSWORD;
  } else if (line == "0") {
    _screen = Screen::MAIN;
    printMainMenu();
  } else {
    printNetworkMenu();
  }
}

// ---------------------------------------------------------------------------
// DHCP / Static IP
// ---------------------------------------------------------------------------

void SerialMenu::printIpModeMenu() {
  SystemConfig& cfg = _routing.get();

  Serial.println();
  Serial.println("============================================================");
  Serial.println(" 1.3 Set DHCP / Static IP");
  Serial.println("============================================================");
  Serial.println();
  Serial.print("  Current Mode     : ");
  Serial.println(cfg.wifi.useDhcp ? "DHCP" : "Static IP");
  Serial.println();
  Serial.println("------------------------------------------------------------");
  Serial.println(" Options");
  Serial.println("------------------------------------------------------------");
  Serial.println("  1. Use DHCP");
  Serial.println("  2. Configure Static IP");
  Serial.println("  0. Back to Network Settings");
  Serial.print("> ");
}

void SerialMenu::handleIpModeMenu(const String& line) {
  SystemConfig& cfg = _routing.get();

  if (line == "1") {
    cfg.wifi.useDhcp = true;
    _storage.save(cfg);
    Serial.println("DHCP selected and saved to flash.");
    printIpModeMenu();
  } else if (line == "2") {
    cfg.wifi.useDhcp = false;
    _storage.save(cfg);
    _screen = Screen::STATIC_IP;
    printStaticIpMenu();
  } else if (line == "0") {
    _screen = Screen::NETWORK;
    printNetworkMenu();
  } else {
    printIpModeMenu();
  }
}

void SerialMenu::printStaticIpMenu() {
  SystemConfig& cfg = _routing.get();

  Serial.println();
  Serial.println("============================================================");
  Serial.println(" 1.3.2 Configure Static IP");
  Serial.println("============================================================");
  Serial.println();
  Serial.print("  IP Address       : ");
  Serial.println(cfg.wifi.staticIp.toIPAddress().toString());
  Serial.print("  Gateway          : ");
  Serial.println(cfg.wifi.gateway.toIPAddress().toString());
  Serial.print("  Subnet Mask      : ");
  Serial.println(cfg.wifi.subnet.toIPAddress().toString());
  Serial.println();
  Serial.println("------------------------------------------------------------");
  Serial.println(" Options");
  Serial.println("------------------------------------------------------------");
  Serial.println("  1. Set IP Address");
  Serial.println("  2. Set Gateway");
  Serial.println("  3. Set Subnet Mask");
  Serial.println("  0. Back to DHCP / Static IP menu");
  Serial.print("> ");
}

void SerialMenu::handleStaticIpMenu(const String& line) {
  if (line == "1") {
    Serial.print("Enter Static IP (a.b.c.d): ");
    _prompt = Prompt::STATIC_IP_VALUE;
  } else if (line == "2") {
    Serial.print("Enter Gateway (a.b.c.d): ");
    _prompt = Prompt::STATIC_GATEWAY_VALUE;
  } else if (line == "3") {
    Serial.print("Enter Subnet Mask (a.b.c.d): ");
    _prompt = Prompt::STATIC_SUBNET_VALUE;
  } else if (line == "0") {
    _screen = Screen::IP_MODE;
    printIpModeMenu();
  } else {
    printStaticIpMenu();
  }
}

// ---------------------------------------------------------------------------
// RS485 Settings
// ---------------------------------------------------------------------------

void SerialMenu::printRs485Menu() {
  SystemConfig& cfg = _routing.get();

  Serial.println();
  Serial.println("============================================================");
  Serial.println(" 2. RS485 Settings");
  Serial.println("============================================================");
  Serial.println();
  Serial.println("  Board            : " BOARD_NAME);
  Serial.println("  UART Port        : " BOARD_RS485_UART_LABEL);
  Serial.print("  RX Pin           : GPIO");
  Serial.println(cfg.rs485RxPin);
  Serial.print("  TX Pin           : GPIO");
  Serial.println(cfg.rs485TxPin);
  Serial.print("  DE/RE Pin        : GPIO");
  Serial.println(cfg.rs485DeRePin);
  Serial.print("  Baudrate         : ");
  Serial.println(cfg.rs485Baudrate);
  Serial.println("  Format           : 8N1");
  Serial.print("  Signal Inversion : ");
  Serial.println(cfg.rs485Invert ? "Inverted (A/B swapped wiring)" : "Normal");
  Serial.println("  Default Mode     : Receive");
  Serial.print("  Input Protocol   : ");
  Serial.println(inputProtocolName(cfg.inputProtocol));
  Serial.print("  Pelco Response   : ");
  Serial.println(pelcoResponseModeName(cfg.pelcoResponseMode));
  Serial.print("  Camera Response  : ");
  Serial.println(responseModeName(cfg.responseMode));
  Serial.print("  Status LED Pin   : GPIO");
  Serial.println(cfg.statusLedPin);
  Serial.print("  Status LED Logic : ");
  Serial.println(cfg.statusLedActiveLow ? "Active Low (LOW = on, e.g. C3 Super Mini onboard LED)"
                                        : "Active High (HIGH = on)");
  Serial.println();
  Serial.println("------------------------------------------------------------");
  Serial.println(" Options");
  Serial.println("------------------------------------------------------------");
  Serial.println("  1. Set Baudrate");
  Serial.println("  2. Set RX Pin");
  Serial.println("  3. Set TX Pin");
  Serial.println("  4. Set DE/RE Pin");
  Serial.println("  5. Set Input Protocol");
  Serial.println("  6. Set Pelco Response Mode");
  Serial.println("  7. Set Status LED Pin");
  Serial.println("  8. Set Signal Inversion");
  Serial.println("  9. Set Camera Response Mode");
  // 숫자가 다 찼다. 항목을 다시 번호 매기면 기존 사용자의 손에 익은 순서가 흐트러지고
  // readme의 메뉴 캡처도 전부 어긋나므로, 새 항목만 문자로 붙인다.
  Serial.println("  a. Set Status LED Polarity");
  Serial.println("  0. Back to Main Menu");
  Serial.print("> ");
}

// 핀 입력 프롬프트 네 벌(RX/TX/DE-RE/Status LED)이 글자 하나만 다른 채 반복하던 것 -
// 빈 줄 취소, 검증, 실패 시 재입력 유도 - 을 한곳에 모은다. 네 벌로 흩어져 있으면
// 규칙을 고칠 때(실제로 C3 포팅에서 전부 바뀌었다) 한 벌을 빠뜨리기 쉽다.
//
// true를 반환하면 *pinOut에 유효한 핀이 담긴 것이고, 호출부가 설정에 반영하면 된다.
// false면 취소됐거나 재입력 프롬프트를 이미 걸어둔 상태라 호출부는 그냥 빠져나가면 된다.
bool SerialMenu::readPinPrompt(const String& line, Prompt retryPrompt, bool isStatusLed,
                                bool requireOutput, uint8_t* pinOut) {
  SystemConfig& cfg = _routing.get();

  if (line.length() == 0) {
    Serial.println("Cancelled.");
    printRs485Menu();
    return false;
  }

  int pin = line.toInt();
  String error;
  bool ok = isStatusLed ? validateStatusLedPin(pin, cfg.rs485RxPin, cfg.rs485TxPin,
                                                cfg.rs485DeRePin, &error)
                        : validateRs485Pin(pin, requireOutput, cfg.statusLedPin, &error);
  if (!ok) {
    Serial.print(error);
    Serial.print(" Try again (blank to cancel): ");
    _prompt = retryPrompt;
    return false;
  }

  *pinOut = (uint8_t)pin;
  return true;
}

// 거부까지는 아니지만 알려야 하는 핀(스트래핑, C3의 부팅 로그 핀)에 대한 경고.
void SerialMenu::printPinWarning(uint8_t pin) {
  const char* warning = gpioWarning(pin);
  if (!warning) return;
  Serial.print("Warning: ");
  Serial.println(warning);
}

void SerialMenu::applyRs485Settings(const SystemConfig& cfg) {
  _storage.save(cfg);
  _rs485.begin(cfg.rs485Baudrate, cfg.rs485RxPin, cfg.rs485TxPin, cfg.rs485DeRePin,
               cfg.rs485Invert);
}

void SerialMenu::handleRs485Menu(const String& line) {
  if (line == "1") {
    Serial.println("1. 2400");
    Serial.println("2. 4800");
    Serial.println("3. 9600");
    Serial.println("4. 38400");
    Serial.println("5. 115200");
    Serial.print("> ");
    _prompt = Prompt::RS485_BAUD_CHOICE;
  } else if (line == "2") {
    Serial.print("Enter RX Pin (GPIO number, blank to cancel): ");
    _prompt = Prompt::RS485_RX_PIN;
  } else if (line == "3") {
    Serial.print("Enter TX Pin (GPIO number, blank to cancel): ");
    _prompt = Prompt::RS485_TX_PIN;
  } else if (line == "4") {
    Serial.print("Enter DE/RE Pin (GPIO number, blank to cancel): ");
    _prompt = Prompt::RS485_DERE_PIN;
  } else if (line == "5") {
    Serial.println("1. VISCA");
    Serial.println("2. Pelco-D");
    Serial.println("3. Pelco-P");
    Serial.println("4. Pelco-D/P Autodetect");
    Serial.print("> ");
    _prompt = Prompt::RS485_INPUT_PROTOCOL_CHOICE;
  } else if (line == "6") {
    Serial.println("1. Respond (synthetic ACK)");
    Serial.println("2. No response");
    Serial.print("> ");
    _prompt = Prompt::RS485_PELCO_RESPONSE_CHOICE;
  } else if (line == "7") {
    Serial.print("Enter Status LED Pin (GPIO number, blank to cancel): ");
    _prompt = Prompt::RS485_STATUS_LED_PIN;
  } else if (line == "8") {
    Serial.println("1. Inverted (A/B swapped wiring)");
    Serial.println("2. Normal");
    Serial.print("> ");
    _prompt = Prompt::RS485_INVERT_CHOICE;
  } else if (line == "9") {
    Serial.println("1. none - drop camera responses (default)");
    Serial.println("2. synthetic - gateway fakes ACK/Completion (VISCA input only)");
    Serial.println("3. forward - send camera response bytes to RS485 as-is");
    Serial.println("4. forward_rewrite - same as forward, but rewrite address to 0x9n");
    Serial.println();
    Serial.println("Note: forward/forward_rewrite send RAW VISCA bytes. A Pelco-D/P controller");
    Serial.println("cannot parse those - repackaging into a Pelco response is not implemented yet.");
    Serial.print("> ");
    _prompt = Prompt::RESPONSE_MODE_CHOICE;
  } else if (line == "a" || line == "A") {
    Serial.println("1. Active High (HIGH = on) - typical external LED to GND");
    Serial.println("2. Active Low  (LOW = on)  - ESP32-C3 Super Mini onboard LED (GPIO8)");
    Serial.print("> ");
    _prompt = Prompt::RS485_LED_POLARITY_CHOICE;
  } else if (line == "0") {
    _screen = Screen::MAIN;
    printMainMenu();
  } else {
    printRs485Menu();
  }
}

// ---------------------------------------------------------------------------
// Routing Table
// ---------------------------------------------------------------------------

void SerialMenu::printRoutingMenu() {
  Serial.println();
  Serial.println("============================================================");
  Serial.println(" 3. Routing Table");
  Serial.println("============================================================");
  Serial.println();
  Serial.println("  Rule:");
  Serial.println("    Camera 1~7 : Forward if IP is configured");
  Serial.println("    No IP      : Ignore");
  Serial.println("    Broadcast  : Send to all configured cameras");
  Serial.println();
  Serial.println("------------------------------------------------------------");
  Serial.println(" Routing Table");
  Serial.println("------------------------------------------------------------");
  Serial.println("  CAM | VISCA | Camera IP       | Port | Protocol          | Addr Mode");
  Serial.println("  ----+-------+-----------------+------+-------------------+--------------");

  for (uint8_t camNumber = 1; camNumber <= CAMERA_SLOT_COUNT; camNumber++) {
    CameraSlot* slot = _routing.camera(camNumber);
    char line[96];
    String ip = slot->isConfigured() ? slot->ip.toIPAddress().toString() : "-";
    snprintf(line, sizeof(line), "   %d  | 0x%02X  | %-15s | %-4u | %-17s | %s", camNumber,
             0x80 | camNumber, ip.c_str(), slot->port, protocolName(slot->protocol).c_str(),
             addressModeName(slot->addressMode).c_str());
    Serial.println(line);
  }

  Serial.println();
  Serial.println("  Broadcast 0x88 : forward to all cameras with IP configured");
  Serial.println();
  Serial.println("------------------------------------------------------------");
  Serial.println(" Options");
  Serial.println("------------------------------------------------------------");
  Serial.println("  1. Select Camera");
  Serial.println("  0. Back to Main Menu");
  Serial.print("> ");
}

void SerialMenu::handleRoutingMenu(const String& line) {
  if (line == "1") {
    Serial.print("Enter camera number (1-7): ");
    _prompt = Prompt::ROUTING_SELECT_CAM;
  } else if (line == "0") {
    _screen = Screen::MAIN;
    printMainMenu();
  } else {
    printRoutingMenu();
  }
}

void SerialMenu::printCameraDetailMenu() {
  CameraSlot* slot = _routing.camera(_pendingCamNumber);

  Serial.println();
  Serial.println("============================================================");
  Serial.print(" 3.");
  Serial.print(_pendingCamNumber);
  Serial.print(" CAM");
  Serial.println(_pendingCamNumber);
  Serial.println("============================================================");
  Serial.println();
  Serial.print("  VISCA Address    : 0x");
  Serial.println(0x80 | _pendingCamNumber, HEX);
  Serial.print("  Camera IP        : ");
  Serial.println(slot->isConfigured() ? slot->ip.toIPAddress().toString() : "(not set)");
  Serial.print("  Port             : ");
  Serial.println(slot->port);
  Serial.print("  Protocol         : ");
  Serial.println(protocolName(slot->protocol));
  Serial.print("  Address Mode     : ");
  Serial.println(addressModeName(slot->addressMode));
  Serial.print("  Auto Power Ctrl  : ");
  Serial.println(slot->autoPowerControl ? "On" : "Off");
  Serial.println();
  Serial.println("------------------------------------------------------------");
  Serial.println(" Options");
  Serial.println("------------------------------------------------------------");
  Serial.println("  1. Set Camera IP");
  Serial.println("  2. Clear Camera IP");
  Serial.println("  3. Set Camera Port");
  Serial.println("  4. Set Protocol");
  Serial.println("  5. Set Address Mode");
  Serial.println("  6. Toggle Auto Power Control");
  Serial.println("  0. Back to Routing Table");
  Serial.print("> ");
}

void SerialMenu::handleCameraDetailMenu(const String& line) {
  if (line == "1") {
    Serial.print("Enter Camera IP (a.b.c.d): ");
    _prompt = Prompt::ROUTING_SET_IP_VALUE;
  } else if (line == "2") {
    _routing.camera(_pendingCamNumber)->ip.fromIPAddress(IPAddress(0, 0, 0, 0));
    _storage.save(_routing.get());
    Serial.println("Camera IP cleared and saved to flash.");
    printCameraDetailMenu();
  } else if (line == "3") {
    Serial.print("Enter Camera Port (blank to cancel): ");
    _prompt = Prompt::ROUTING_SET_PORT_VALUE;
  } else if (line == "4") {
    Serial.println("1. IP_VISCA_RAW_UDP");
    Serial.println("2. IP_VISCA_RAW_TCP");
    Serial.println("3. SONY_VISCA_UDP");
    Serial.print("> ");
    _prompt = Prompt::ROUTING_SET_PROTOCOL_VALUE;
  } else if (line == "5") {
    Serial.println("1. rewrite_0x81");
    Serial.println("2. preserve");
    Serial.println("3. rewrite_by_cam");
    Serial.print("> ");
    _prompt = Prompt::ROUTING_SET_ADDRMODE_VALUE;
  } else if (line == "6") {
    CameraSlot* slot = _routing.camera(_pendingCamNumber);
    slot->autoPowerControl = !slot->autoPowerControl;
    _storage.save(_routing.get());
    Serial.println(slot->autoPowerControl ? "Auto Power Control enabled and saved to flash."
                                           : "Auto Power Control disabled and saved to flash.");
    printCameraDetailMenu();
  } else if (line == "0") {
    _screen = Screen::ROUTING;
    printRoutingMenu();
  } else {
    printCameraDetailMenu();
  }
}

// ---------------------------------------------------------------------------
// Counters
// ---------------------------------------------------------------------------

void SerialMenu::printCountersMenu() {
  Serial.println();
  Serial.println("============================================================");
  Serial.println(" 4. Counters");
  Serial.println("============================================================");
  Serial.println();
  Serial.print("  RS485 RX Total        : "); Serial.println(_diagnostics.rs485RxTotal());
  Serial.print("  Forwarded             : "); Serial.println(_diagnostics.forwarded());
  Serial.print("  Ignored No IP         : "); Serial.println(_diagnostics.ignoredNoIp());
  Serial.print("  Broadcast RX          : "); Serial.println(_diagnostics.broadcastRx());
  Serial.print("  Broadcast Forwarded   : "); Serial.println(_diagnostics.broadcastForwarded());
  Serial.print("  IP TX Success         : "); Serial.println(_diagnostics.ipTxSuccess());
  Serial.print("  IP TX Failed          : "); Serial.println(_diagnostics.ipTxFailed());
  Serial.print("  RS485 TX Response     : "); Serial.println(_diagnostics.rs485TxResponse());
  Serial.print("  Web RS485 TX          : "); Serial.println(_diagnostics.webTx());
  Serial.print("  Web RS485 TX Dropped  : "); Serial.println(_diagnostics.webTxDropped());
  Serial.print("  Malformed Packet      : "); Serial.println(_diagnostics.malformedPacket());
  Serial.print("  Buffer Overflow       : "); Serial.println(_diagnostics.bufferOverflow());
  Serial.print("  Packet Timeout        : "); Serial.println(_diagnostics.packetTimeout());
  Serial.print("  Wi-Fi Reconnect       : "); Serial.println(_diagnostics.wifiReconnect());
  Serial.print("  Uptime                : "); Serial.println(_diagnostics.uptimeString());
  Serial.println();
  Serial.println("------------------------------------------------------------");
  Serial.println(" Options");
  Serial.println("------------------------------------------------------------");
  Serial.println("  1. Reset Counters");
  Serial.println("  0. Back to Main Menu");
  Serial.println("  (Press Enter with no input to refresh)");
  Serial.print("> ");
}

void SerialMenu::handleCountersMenu(const String& line) {
  if (line == "1") {
    _diagnostics.resetCounters();
    Serial.println("Counters reset.");
    printCountersMenu();
  } else if (line == "0") {
    _screen = Screen::MAIN;
    printMainMenu();
  } else {
    printCountersMenu();
  }
}

// ---------------------------------------------------------------------------
// Debug Mode
// ---------------------------------------------------------------------------

void SerialMenu::printDebugMenu() {
  SystemConfig& cfg = _routing.get();

  Serial.println();
  Serial.println("============================================================");
  Serial.println(" 5. Debug Mode");
  Serial.println("============================================================");
  Serial.println();
  Serial.print("  Current Debug Mode : ");
  Serial.println(cfg.debugMode ? "ON" : "OFF");
  Serial.println();
  Serial.println("------------------------------------------------------------");
  Serial.println(" Options");
  Serial.println("------------------------------------------------------------");
  Serial.println("  1. Debug ON");
  Serial.println("  2. Debug OFF");
  Serial.println("  3. Show Last 20 Packets");
  Serial.println("  4. Live Packet Monitor");
  Serial.println("  5. Raw Byte Monitor");
  Serial.println("  6. Send Test Command");
  Serial.println("  7. Show Unhandled Commands");
  Serial.println("  0. Back to Main Menu");
  Serial.print("> ");
}

void SerialMenu::handleDebugMenu(const String& line) {
  SystemConfig& cfg = _routing.get();

  if (line == "1") {
    cfg.debugMode = true;
    _storage.save(cfg);
    Serial.println("Debug mode ON.");
    printDebugMenu();
  } else if (line == "2") {
    cfg.debugMode = false;
    _storage.save(cfg);
    Serial.println("Debug mode OFF.");
    printDebugMenu();
  } else if (line == "3") {
    Serial.println("---- Last packets (most recent first) ----");
    for (uint8_t i = 0; i < _diagnostics.recentLogDepth(); i++) {
      const String& entry = _diagnostics.recentLog()[i];
      if (entry.length() == 0) continue;
      Serial.println(entry);
    }
    printDebugMenu();
  } else if (line == "4") {
    _screen = Screen::DEBUG_LIVE;
    printDebugLiveMenu();
  } else if (line == "5") {
    _screen = Screen::DEBUG_RAW;
    printDebugRawMenu();
  } else if (line == "6") {
    Serial.println();
    Serial.println("Sends a Query Pan Position command (address 1) out on RS485 so you can");
    Serial.println("check whether anything on the bus responds. Switches to Raw Byte Monitor");
    Serial.println("right after sending so the response (if any) is visible either way, even");
    Serial.println("if it isn't a well-formed Pelco-D/P reply.");
    Serial.println("1. Pelco-D");
    Serial.println("2. Pelco-P");
    Serial.print("> ");
    _prompt = Prompt::DEBUG_TEST_CMD_PROTOCOL_CHOICE;
  } else if (line == "7") {
    Serial.println("---- Unhandled commands (most recent first, Debug Mode not required) ----");
    uint8_t count = _diagnostics.unhandledCount();
    if (count == 0) {
      Serial.println("(none yet)");
    } else {
      for (uint8_t i = 0; i < count; i++) {
        Serial.println(_diagnostics.unhandledEntry(i));
      }
    }
    printDebugMenu();
  } else if (line == "0") {
    _screen = Screen::MAIN;
    printMainMenu();
  } else {
    printDebugMenu();
  }
}

void SerialMenu::printDebugLiveMenu() {
  SystemConfig& cfg = _routing.get();
  // Live Packet Monitor를 보려면 debugMode가 켜져 있어야 [RX]/[TX] 로그가 찍히지만,
  // 이건 "지금 화면을 보는 동안만" 필요한 상태다 - flash에 영구 저장하면 여길 한 번만
  // 들어와도 재부팅 후에까지 debugMode가 계속 ON으로 남는다. 나갈 때 원래 값으로
  // 되돌리므로 여기서는 메모리에서만 켜고 저장하지 않는다.
  _debugModeBeforeLive = cfg.debugMode;
  cfg.debugMode = true;

  Serial.println();
  Serial.println("============================================================");
  Serial.println(" 5.4 Live Packet Monitor");
  Serial.println("============================================================");
  Serial.println("Streaming RS485 <-> IP VISCA traffic below.");
  Serial.println("Press Enter (no input) to return to Debug Mode menu.");
  Serial.println("------------------------------------------------------------");
}

void SerialMenu::handleDebugLiveMenu(const String& line) {
  if (line.length() == 0) {
    Serial.println("------------------------------------------------------------");
    _routing.get().debugMode = _debugModeBeforeLive;
    _screen = Screen::DEBUG;
    printDebugMenu();
  } else {
    Serial.println("(still monitoring - press Enter with no input to return)");
  }
}

// Live Packet Monitor와 달리 프로토콜 파싱/체크섬 통과 여부와 무관하게 RS485에서
// 읽히는 모든 바이트를 그대로 hex로 보여준다. main.cpp의 loop()가 rawMonitorActive()를
// 매 바이트/매 회전마다 확인해서 실제 echo를 수행한다 - 이 화면은 그 트리거일 뿐이다.
void SerialMenu::printDebugRawMenu() {
  Serial.println();
  Serial.println("============================================================");
  Serial.println(" 5.5 Raw Byte Monitor");
  Serial.println("============================================================");
  Serial.println("Streaming raw RS485 bytes below, regardless of protocol/checksum.");
  Serial.println("Useful for diagnosing wiring/baudrate/protocol mismatches.");
  Serial.println("Press Enter (no input) to return to Debug Mode menu.");
  Serial.println("------------------------------------------------------------");
}

void SerialMenu::handleDebugRawMenu(const String& line) {
  if (line.length() == 0) {
    Serial.println();
    Serial.println("------------------------------------------------------------");
    _screen = Screen::DEBUG;
    printDebugMenu();
  } else {
    Serial.println("(still monitoring - press Enter with no input to return)");
  }
}

// ---------------------------------------------------------------------------
// Multi-step prompts
// ---------------------------------------------------------------------------

void SerialMenu::handlePrompt(const String& line) {
  SystemConfig& cfg = _routing.get();
  Prompt prompt = _prompt;
  _prompt = Prompt::NONE;

  switch (prompt) {
    case Prompt::WIFI_SSID_CHOICE: {
      int choice = line.toInt();
      if (choice == 1) {
        WiFi.scanDelete();
        Serial.print("Enter Wi-Fi SSID: ");
        _prompt = Prompt::WIFI_SSID;
      } else if (choice == 0) {
        WiFi.scanDelete();
        _screen = Screen::MAIN;
        printMainMenu();
      } else if (choice >= 2 && choice <= _scanCount + 1) {
        String ssid = WiFi.SSID(choice - 2);
        ssid.toCharArray(cfg.wifi.ssid, sizeof(cfg.wifi.ssid));
        _storage.save(cfg);
        Serial.print("SSID set to: ");
        Serial.print(ssid);
        Serial.println(" (saved to flash)");
        WiFi.scanDelete();
        printNetworkMenu();
      } else {
        Serial.println("Invalid choice.");
        WiFi.scanDelete();
        printNetworkMenu();
      }
      break;
    }
    case Prompt::WIFI_SSID: {
      line.toCharArray(cfg.wifi.ssid, sizeof(cfg.wifi.ssid));
      _storage.save(cfg);
      Serial.println("SSID set and saved to flash.");
      printNetworkMenu();
      break;
    }
    case Prompt::WIFI_PASSWORD: {
      line.toCharArray(cfg.wifi.password, sizeof(cfg.wifi.password));
      _storage.save(cfg);
      Serial.println("Password set and saved to flash.");
      printNetworkMenu();
      break;
    }
    case Prompt::AP_SSID: {
      if (line.length() == 0) {
        Serial.println("Cancelled.");
      } else {
        line.toCharArray(cfg.wifi.apSsid, sizeof(cfg.wifi.apSsid));
        _storage.save(cfg);
        applyApSettings(cfg);
        Serial.println("AP SSID updated and saved to flash.");
      }
      printNetworkMenu();
      break;
    }
    case Prompt::AP_PASSWORD: {
      if (line.length() == 0) {
        Serial.println("Cancelled.");
      } else {
        line.toCharArray(cfg.wifi.apPassword, sizeof(cfg.wifi.apPassword));
        _storage.save(cfg);
        applyApSettings(cfg);
        Serial.println("AP Password updated and saved to flash.");
      }
      printNetworkMenu();
      break;
    }
    case Prompt::STATIC_IP_VALUE: {
      IPAddress ip;
      if (ip.fromString(line)) {
        cfg.wifi.staticIp.fromIPAddress(ip);
        _storage.save(cfg);
        Serial.println("IP Address set and saved to flash.");
        printStaticIpMenu();
      } else {
        Serial.print("Invalid IP, try again: ");
        _prompt = Prompt::STATIC_IP_VALUE;
      }
      break;
    }
    case Prompt::STATIC_GATEWAY_VALUE: {
      IPAddress ip;
      if (ip.fromString(line)) {
        cfg.wifi.gateway.fromIPAddress(ip);
        _storage.save(cfg);
        Serial.println("Gateway set and saved to flash.");
        printStaticIpMenu();
      } else {
        Serial.print("Invalid IP, try again: ");
        _prompt = Prompt::STATIC_GATEWAY_VALUE;
      }
      break;
    }
    case Prompt::STATIC_SUBNET_VALUE: {
      IPAddress ip;
      if (ip.fromString(line)) {
        cfg.wifi.subnet.fromIPAddress(ip);
        _storage.save(cfg);
        Serial.println("Subnet Mask set and saved to flash.");
        printStaticIpMenu();
      } else {
        Serial.print("Invalid IP, try again: ");
        _prompt = Prompt::STATIC_SUBNET_VALUE;
      }
      break;
    }
    case Prompt::RS485_BAUD_CHOICE: {
      int choice = line.toInt();
      if (choice >= 1 && choice <= 5) {
        cfg.rs485Baudrate = kBaudChoices[choice - 1];
        applyRs485Settings(cfg);
        Serial.println("Baudrate set and saved to flash.");
      } else {
        Serial.println("Invalid choice.");
      }
      printRs485Menu();
      break;
    }
    case Prompt::RS485_INPUT_PROTOCOL_CHOICE: {
      if (line == "1") {
        cfg.inputProtocol = InputProtocol::VISCA;
        _storage.save(cfg);
        Serial.println("Input protocol set to VISCA and saved to flash.");
      } else if (line == "2") {
        cfg.inputProtocol = InputProtocol::PELCO_D;
        _storage.save(cfg);
        Serial.println("Input protocol set to Pelco-D and saved to flash.");
      } else if (line == "3") {
        cfg.inputProtocol = InputProtocol::PELCO_P;
        _storage.save(cfg);
        Serial.println("Input protocol set to Pelco-P and saved to flash.");
      } else if (line == "4") {
        cfg.inputProtocol = InputProtocol::PELCO_AUTO;
        _storage.save(cfg);
        Serial.println("Input protocol set to Pelco-D/P Autodetect and saved to flash.");
      } else {
        Serial.println("Invalid choice.");
      }
      printRs485Menu();
      break;
    }
    case Prompt::RS485_PELCO_RESPONSE_CHOICE: {
      if (line == "1") {
        cfg.pelcoResponseMode = PelcoResponseMode::SYNTHETIC;
        _storage.save(cfg);
        Serial.println("Pelco response mode set to Respond and saved to flash.");
      } else if (line == "2") {
        cfg.pelcoResponseMode = PelcoResponseMode::NONE;
        _storage.save(cfg);
        Serial.println("Pelco response mode set to No response and saved to flash.");
      } else {
        Serial.println("Invalid choice.");
      }
      printRs485Menu();
      break;
    }
    case Prompt::RESPONSE_MODE_CHOICE: {
      if (line == "1" || line == "2" || line == "3" || line == "4") {
        cfg.responseMode = (ResponseMode)(line.toInt() - 1);
        _storage.save(cfg);
        Serial.print("Camera response mode set to ");
        Serial.print(responseModeName(cfg.responseMode));
        Serial.println(" and saved to flash.");
      } else {
        Serial.println("Invalid choice.");
      }
      printRs485Menu();
      break;
    }
    case Prompt::RS485_INVERT_CHOICE: {
      if (line == "1" || line == "2") {
        cfg.rs485Invert = (line == "1");
        // applyRs485Settings()가 UART를 새 반전 설정으로 다시 초기화한다 - 재부팅
        // 없이 바로 적용된다.
        applyRs485Settings(cfg);
        Serial.print("Signal inversion set to ");
        Serial.print(cfg.rs485Invert ? "Inverted" : "Normal");
        Serial.println(" and saved to flash.");
      } else {
        Serial.println("Invalid choice.");
      }
      printRs485Menu();
      break;
    }
    case Prompt::RS485_RX_PIN: {
      uint8_t pin;
      if (!readPinPrompt(line, Prompt::RS485_RX_PIN, /*isStatusLed=*/false, /*requireOutput=*/false, &pin)) {
        break;
      }
      cfg.rs485RxPin = pin;
      applyRs485Settings(cfg);
      printPinWarning(pin);
      Serial.println("RX pin set and saved to flash.");
      printRs485Menu();
      break;
    }
    case Prompt::RS485_TX_PIN: {
      uint8_t pin;
      if (!readPinPrompt(line, Prompt::RS485_TX_PIN, /*isStatusLed=*/false, /*requireOutput=*/true, &pin)) {
        break;
      }
      cfg.rs485TxPin = pin;
      applyRs485Settings(cfg);
      printPinWarning(pin);
      Serial.println("TX pin set and saved to flash.");
      printRs485Menu();
      break;
    }
    case Prompt::RS485_DERE_PIN: {
      uint8_t pin;
      if (!readPinPrompt(line, Prompt::RS485_DERE_PIN, /*isStatusLed=*/false, /*requireOutput=*/true, &pin)) {
        break;
      }
      cfg.rs485DeRePin = pin;
      applyRs485Settings(cfg);
      printPinWarning(pin);
      Serial.println("DE/RE pin set and saved to flash.");
      printRs485Menu();
      break;
    }
    case Prompt::RS485_STATUS_LED_PIN: {
      uint8_t pin;
      if (!readPinPrompt(line, Prompt::RS485_STATUS_LED_PIN, /*isStatusLed=*/true,
                         /*requireOutput=*/true, &pin)) {
        break;
      }
      cfg.statusLedPin = pin;
      _storage.save(cfg);
      _statusLed.begin(cfg.statusLedPin, cfg.statusLedActiveLow);
      printPinWarning(pin);
      Serial.println("Status LED pin set and saved to flash.");
      printRs485Menu();
      break;
    }
    case Prompt::RS485_LED_POLARITY_CHOICE: {
      if (line == "1" || line == "2") {
        cfg.statusLedActiveLow = (line == "2");
        _storage.save(cfg);
        // 극성만 바꿔도 LED를 다시 열어야 한다 - 안 그러면 다음 상태 변화가 올 때까지
        // 이전 극성으로 켜둔 레벨이 그대로 남는다.
        _statusLed.begin(cfg.statusLedPin, cfg.statusLedActiveLow);
        Serial.print("Status LED polarity set to ");
        Serial.print(cfg.statusLedActiveLow ? "Active Low" : "Active High");
        Serial.println(" and saved to flash.");
      } else {
        Serial.println("Invalid choice.");
      }
      printRs485Menu();
      break;
    }
    case Prompt::ROUTING_SELECT_CAM: {
      int cam = line.toInt();
      if (cam >= 1 && cam <= CAMERA_SLOT_COUNT) {
        _pendingCamNumber = (uint8_t)cam;
        _screen = Screen::CAMERA_DETAIL;
        printCameraDetailMenu();
      } else {
        Serial.println("Invalid camera number.");
        printRoutingMenu();
      }
      break;
    }
    case Prompt::ROUTING_SET_IP_VALUE: {
      IPAddress ip;
      if (ip.fromString(line)) {
        _routing.camera(_pendingCamNumber)->ip.fromIPAddress(ip);
        _storage.save(cfg);
        Serial.println("Camera IP set and saved to flash.");
        printCameraDetailMenu();
      } else {
        Serial.print("Invalid IP, try again: ");
        _prompt = Prompt::ROUTING_SET_IP_VALUE;
      }
      break;
    }
    case Prompt::ROUTING_SET_PORT_VALUE: {
      if (line.length() == 0) {
        Serial.println("Cancelled.");
        printCameraDetailMenu();
        break;
      }
      int port = line.toInt();
      if (port > 0 && port <= 65535) {
        _routing.camera(_pendingCamNumber)->port = (uint16_t)port;
        _storage.save(cfg);
        Serial.println("Camera port set and saved to flash.");
        printCameraDetailMenu();
      } else {
        Serial.print("Invalid port, try again (blank to cancel): ");
        _prompt = Prompt::ROUTING_SET_PORT_VALUE;
      }
      break;
    }
    case Prompt::ROUTING_SET_PROTOCOL_VALUE: {
      int choice = line.toInt();
      if (choice >= 1 && choice <= 3) {
        _routing.camera(_pendingCamNumber)->protocol = (ProtocolMode)(choice - 1);
        _storage.save(cfg);
        Serial.println("Protocol set and saved to flash.");
        printCameraDetailMenu();
      } else {
        Serial.println("Invalid choice.");
        printCameraDetailMenu();
      }
      break;
    }
    case Prompt::ROUTING_SET_ADDRMODE_VALUE: {
      int choice = line.toInt();
      if (choice >= 1 && choice <= 3) {
        _routing.camera(_pendingCamNumber)->addressMode = (AddressMode)(choice - 1);
        _storage.save(cfg);
        Serial.println("Address mode set and saved to flash.");
        printCameraDetailMenu();
      } else {
        Serial.println("Invalid choice.");
        printCameraDetailMenu();
      }
      break;
    }
    case Prompt::FACTORY_RESET_CONFIRM: {
      if (line == "YES") {
        Serial.println("Factory reset confirmed. Restoring defaults and rebooting...");
        performFactoryReset(_routing, _storage, _rs485, _statusLed);
      } else {
        Serial.println("Cancelled.");
        printMainMenu();
      }
      break;
    }
    case Prompt::DEBUG_TEST_CMD_PROTOCOL_CHOICE: {
      if (line == "1") {
        uint8_t packet[7];
        buildPelcoDTestCommand(packet);
        _rs485.writePacket(packet, sizeof(packet));
        Serial.print("Sent (Pelco-D): ");
        Serial.println(viscaBytesToHex(packet, sizeof(packet)));
      } else if (line == "2") {
        uint8_t packet[8];
        buildPelcoPTestCommand(packet);
        _rs485.writePacket(packet, sizeof(packet));
        Serial.print("Sent (Pelco-P): ");
        Serial.println(viscaBytesToHex(packet, sizeof(packet)));
      } else {
        Serial.println("Invalid choice.");
        printDebugMenu();
        break;
      }
      Serial.println("Watching for a response (Raw Byte Monitor)...");
      _screen = Screen::DEBUG_RAW;
      printDebugRawMenu();
      break;
    }
    case Prompt::NONE:
    default:
      break;
  }
}
