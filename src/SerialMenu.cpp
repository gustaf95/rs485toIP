#include "SerialMenu.h"
#include <WiFi.h>

namespace {
const uint32_t kBaudChoices[5] = {2400, 4800, 9600, 38400, 115200};
const int kMaxGpio = 39;

// GPIO1/3: UART0 (USB Serial 메뉴 전용), GPIO6~11: 내장 SPI Flash 전용 - 절대 사용 불가.
bool isReservedGpio(uint8_t pin) {
  return pin == 1 || pin == 3 || (pin >= 6 && pin <= 11);
}

// GPIO34~39: 입력 전용 - 출력(TX, DE/RE)으로는 사용 불가.
bool isInputOnlyGpio(uint8_t pin) {
  return pin >= 34 && pin <= 39;
}

// 부팅 모드를 결정하는 스트래핑 핀 - 사용은 가능하나 외부 배선에 따라 부팅에 영향을 줄 수 있어 경고만 표시.
bool isStrappingGpio(uint8_t pin) {
  return pin == 0 || pin == 2 || pin == 5 || pin == 12 || pin == 15;
}

bool validateRs485Pin(int pin, bool requireOutput, String* errorOut) {
  if (pin < 0 || pin > kMaxGpio) {
    *errorOut = "Invalid GPIO number (0-39).";
    return false;
  }
  if (pin == STATUS_LED_PIN) {
    *errorOut = "GPIO" + String(pin) + " is reserved for the status LED.";
    return false;
  }
  if (isReservedGpio((uint8_t)pin)) {
    *errorOut = "GPIO" + String(pin) + " is reserved (UART0 console or internal SPI flash).";
    return false;
  }
  if (requireOutput && isInputOnlyGpio((uint8_t)pin)) {
    *errorOut = "GPIO" + String(pin) + " is input-only; cannot be used here.";
    return false;
  }
  return true;
}
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

void SerialMenu::begin() {
  printMainMenu();
}

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
  Serial.print("  Wi-Fi Mode       : STA\n");
  Serial.print("  Wi-Fi Status     : ");
  Serial.println(connected ? "Connected" : "Disconnected");
  Serial.print("  SSID             : ");
  Serial.println(strlen(cfg.wifi.ssid) ? cfg.wifi.ssid : "(not set)");
  Serial.print("  DHCP             : ");
  Serial.println(cfg.wifi.useDhcp ? "Enabled" : "Disabled");
  Serial.print("  ESP32 IP         : ");
  Serial.println(connected ? WiFi.localIP().toString() : "Not assigned");
  Serial.print("  Gateway          : ");
  Serial.println(cfg.wifi.gateway.toIPAddress().toString());
  Serial.print("  Subnet           : ");
  Serial.println(cfg.wifi.subnet.toIPAddress().toString());
  Serial.println();
  Serial.println("------------------------------------------------------------");
  Serial.println(" Options");
  Serial.println("------------------------------------------------------------");
  Serial.println("  1. Set Wi-Fi SSID");
  Serial.println("  2. Set Wi-Fi Password");
  Serial.println("  3. Set DHCP / Static IP");
  Serial.println("  4. Retry Wi-Fi Connection");
  Serial.println("  0. Back to Main Menu");
  Serial.print("> ");
}

void SerialMenu::handleNetworkMenu(const String& line) {
  if (line == "1") {
    Serial.println("Scanning Wi-Fi networks...");
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
  Serial.println("  UART Port        : UART2 / Serial2");
  Serial.print("  RX Pin           : GPIO");
  Serial.println(cfg.rs485RxPin);
  Serial.print("  TX Pin           : GPIO");
  Serial.println(cfg.rs485TxPin);
  Serial.print("  DE/RE Pin        : GPIO");
  Serial.println(cfg.rs485DeRePin);
  Serial.print("  Baudrate         : ");
  Serial.println(cfg.rs485Baudrate);
  Serial.println("  Format           : 8N1");
  Serial.println("  Default Mode     : Receive");
  Serial.println();
  Serial.println("------------------------------------------------------------");
  Serial.println(" Options");
  Serial.println("------------------------------------------------------------");
  Serial.println("  1. Set Baudrate");
  Serial.println("  2. Set RX Pin");
  Serial.println("  3. Set TX Pin");
  Serial.println("  4. Set DE/RE Pin");
  Serial.println("  0. Back to Main Menu");
  Serial.print("> ");
}

void SerialMenu::applyRs485Settings(const SystemConfig& cfg) {
  _storage.save(cfg);
  _rs485.begin(cfg.rs485Baudrate, cfg.rs485RxPin, cfg.rs485TxPin, cfg.rs485DeRePin);
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
  Serial.println();
  Serial.println("------------------------------------------------------------");
  Serial.println(" Options");
  Serial.println("------------------------------------------------------------");
  Serial.println("  1. Set Camera IP");
  Serial.println("  2. Clear Camera IP");
  Serial.println("  3. Set Camera Port");
  Serial.println("  4. Set Protocol");
  Serial.println("  5. Set Address Mode");
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
  } else if (line == "0") {
    _screen = Screen::MAIN;
    printMainMenu();
  } else {
    printDebugMenu();
  }
}

void SerialMenu::printDebugLiveMenu() {
  SystemConfig& cfg = _routing.get();
  if (!cfg.debugMode) {
    cfg.debugMode = true;
    _storage.save(cfg);
  }

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
    case Prompt::RS485_RX_PIN: {
      if (line.length() == 0) {
        Serial.println("Cancelled.");
        printRs485Menu();
        break;
      }
      int pin = line.toInt();
      String error;
      if (!validateRs485Pin(pin, /*requireOutput=*/false, &error)) {
        Serial.print(error);
        Serial.print(" Try again (blank to cancel): ");
        _prompt = Prompt::RS485_RX_PIN;
        break;
      }
      cfg.rs485RxPin = (uint8_t)pin;
      applyRs485Settings(cfg);
      if (isStrappingGpio((uint8_t)pin)) {
        Serial.println("Warning: GPIO is a boot strapping pin - verify no external pull affects boot.");
      }
      Serial.println("RX pin set and saved to flash.");
      printRs485Menu();
      break;
    }
    case Prompt::RS485_TX_PIN: {
      if (line.length() == 0) {
        Serial.println("Cancelled.");
        printRs485Menu();
        break;
      }
      int pin = line.toInt();
      String error;
      if (!validateRs485Pin(pin, /*requireOutput=*/true, &error)) {
        Serial.print(error);
        Serial.print(" Try again (blank to cancel): ");
        _prompt = Prompt::RS485_TX_PIN;
        break;
      }
      cfg.rs485TxPin = (uint8_t)pin;
      applyRs485Settings(cfg);
      if (isStrappingGpio((uint8_t)pin)) {
        Serial.println("Warning: GPIO is a boot strapping pin - verify no external pull affects boot.");
      }
      Serial.println("TX pin set and saved to flash.");
      printRs485Menu();
      break;
    }
    case Prompt::RS485_DERE_PIN: {
      if (line.length() == 0) {
        Serial.println("Cancelled.");
        printRs485Menu();
        break;
      }
      int pin = line.toInt();
      String error;
      if (!validateRs485Pin(pin, /*requireOutput=*/true, &error)) {
        Serial.print(error);
        Serial.print(" Try again (blank to cancel): ");
        _prompt = Prompt::RS485_DERE_PIN;
        break;
      }
      cfg.rs485DeRePin = (uint8_t)pin;
      applyRs485Settings(cfg);
      if (isStrappingGpio((uint8_t)pin)) {
        Serial.println("Warning: GPIO is a boot strapping pin - verify no external pull affects boot.");
      }
      Serial.println("DE/RE pin set and saved to flash.");
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
    case Prompt::NONE:
    default:
      break;
  }
}
