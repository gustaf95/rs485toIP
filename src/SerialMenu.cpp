#include "SerialMenu.h"
#include <WiFi.h>

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

void SerialMenu::begin() {
  printMainMenu();
}

void SerialMenu::poll() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      Serial.println();
      String line = _lineBuffer;
      line.trim();
      _lineBuffer = "";
      handleLine(line);
    } else {
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
    case Screen::RS485: handleRs485Menu(line); break;
    case Screen::ROUTING: handleRoutingMenu(line); break;
    case Screen::COUNTERS: handleCountersMenu(line); break;
    case Screen::DEBUG: handleDebugMenu(line); break;
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
  Serial.println("  5. Save Network Settings");
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
    Serial.println("1. DHCP");
    Serial.println("2. Static");
    Serial.print("> ");
    _prompt = Prompt::WIFI_MODE_CHOICE;
  } else if (line == "4") {
    Serial.println("Retrying Wi-Fi connection...");
    if (_wifiRetry) _wifiRetry();
    printNetworkMenu();
  } else if (line == "5") {
    _storage.save(_routing.get());
    Serial.println("Network settings saved.");
    printNetworkMenu();
  } else if (line == "0") {
    _screen = Screen::MAIN;
    printMainMenu();
  } else {
    printNetworkMenu();
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
  Serial.println("  5. Save RS485 Settings");
  Serial.println("  0. Back to Main Menu");
  Serial.print("> ");
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
    Serial.print("Enter RX Pin (GPIO number): ");
    _prompt = Prompt::RS485_RX_PIN;
  } else if (line == "3") {
    Serial.print("Enter TX Pin (GPIO number): ");
    _prompt = Prompt::RS485_TX_PIN;
  } else if (line == "4") {
    Serial.print("Enter DE/RE Pin (GPIO number): ");
    _prompt = Prompt::RS485_DERE_PIN;
  } else if (line == "5") {
    SystemConfig& cfg = _routing.get();
    _storage.save(cfg);
    _rs485.begin(cfg.rs485Baudrate, cfg.rs485RxPin, cfg.rs485TxPin, cfg.rs485DeRePin);
    Serial.println("RS485 settings saved and applied.");
    printRs485Menu();
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
  Serial.println("  1. Set Camera IP");
  Serial.println("  2. Clear Camera IP");
  Serial.println("  3. Set Camera Port");
  Serial.println("  4. Set Protocol");
  Serial.println("  5. Set Address Mode");
  Serial.println("  6. Save Routing Table");
  Serial.println("  0. Back to Main Menu");
  Serial.print("> ");
}

void SerialMenu::handleRoutingMenu(const String& line) {
  if (line == "1") {
    Serial.print("Enter camera number (1-7): ");
    _prompt = Prompt::ROUTING_SET_IP_CAM;
  } else if (line == "2") {
    Serial.print("Enter camera number (1-7): ");
    _prompt = Prompt::ROUTING_CLEAR_IP_CAM;
  } else if (line == "3") {
    Serial.print("Enter camera number (1-7): ");
    _prompt = Prompt::ROUTING_SET_PORT_CAM;
  } else if (line == "4") {
    Serial.print("Enter camera number (1-7): ");
    _prompt = Prompt::ROUTING_SET_PROTOCOL_CAM;
  } else if (line == "5") {
    Serial.print("Enter camera number (1-7): ");
    _prompt = Prompt::ROUTING_SET_ADDRMODE_CAM;
  } else if (line == "6") {
    _storage.save(_routing.get());
    Serial.println("Routing table saved.");
    printRoutingMenu();
  } else if (line == "0") {
    _screen = Screen::MAIN;
    printMainMenu();
  } else {
    printRoutingMenu();
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
  } else if (line == "0") {
    _screen = Screen::MAIN;
    printMainMenu();
  } else {
    printDebugMenu();
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
        Serial.print("SSID set to: ");
        Serial.println(ssid);
        Serial.println("Remember to Save Network Settings.");
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
      Serial.println("SSID set. Remember to Save Network Settings.");
      printNetworkMenu();
      break;
    }
    case Prompt::WIFI_PASSWORD: {
      line.toCharArray(cfg.wifi.password, sizeof(cfg.wifi.password));
      Serial.println("Password set. Remember to Save Network Settings.");
      printNetworkMenu();
      break;
    }
    case Prompt::WIFI_MODE_CHOICE: {
      if (line == "1") {
        cfg.wifi.useDhcp = true;
        Serial.println("DHCP selected. Remember to Save Network Settings.");
        printNetworkMenu();
      } else if (line == "2") {
        cfg.wifi.useDhcp = false;
        Serial.print("Enter Static IP (a.b.c.d): ");
        _prompt = Prompt::WIFI_STATIC_IP;
      } else {
        Serial.println("Invalid choice.");
        printNetworkMenu();
      }
      break;
    }
    case Prompt::WIFI_STATIC_IP: {
      IPAddress ip;
      if (ip.fromString(line)) {
        cfg.wifi.staticIp.fromIPAddress(ip);
        Serial.print("Enter Gateway (a.b.c.d): ");
        _prompt = Prompt::WIFI_GATEWAY;
      } else {
        Serial.print("Invalid IP, try again: ");
        _prompt = Prompt::WIFI_STATIC_IP;
      }
      break;
    }
    case Prompt::WIFI_GATEWAY: {
      IPAddress ip;
      if (ip.fromString(line)) {
        cfg.wifi.gateway.fromIPAddress(ip);
        Serial.print("Enter Subnet Mask (a.b.c.d): ");
        _prompt = Prompt::WIFI_SUBNET;
      } else {
        Serial.print("Invalid IP, try again: ");
        _prompt = Prompt::WIFI_GATEWAY;
      }
      break;
    }
    case Prompt::WIFI_SUBNET: {
      IPAddress ip;
      if (ip.fromString(line)) {
        cfg.wifi.subnet.fromIPAddress(ip);
        Serial.println("Static IP settings set. Remember to Save Network Settings.");
        printNetworkMenu();
      } else {
        Serial.print("Invalid IP, try again: ");
        _prompt = Prompt::WIFI_SUBNET;
      }
      break;
    }
    case Prompt::RS485_BAUD_CHOICE: {
      int choice = line.toInt();
      if (choice >= 1 && choice <= 5) {
        cfg.rs485Baudrate = kBaudChoices[choice - 1];
        Serial.println("Baudrate set. Remember to Save RS485 Settings.");
      } else {
        Serial.println("Invalid choice.");
      }
      printRs485Menu();
      break;
    }
    case Prompt::RS485_RX_PIN: {
      cfg.rs485RxPin = (uint8_t)line.toInt();
      Serial.println("RX pin set. Remember to Save RS485 Settings.");
      printRs485Menu();
      break;
    }
    case Prompt::RS485_TX_PIN: {
      cfg.rs485TxPin = (uint8_t)line.toInt();
      Serial.println("TX pin set. Remember to Save RS485 Settings.");
      printRs485Menu();
      break;
    }
    case Prompt::RS485_DERE_PIN: {
      cfg.rs485DeRePin = (uint8_t)line.toInt();
      Serial.println("DE/RE pin set. Remember to Save RS485 Settings.");
      printRs485Menu();
      break;
    }
    case Prompt::ROUTING_SET_IP_CAM: {
      int cam = line.toInt();
      if (cam >= 1 && cam <= CAMERA_SLOT_COUNT) {
        _pendingCamNumber = (uint8_t)cam;
        Serial.print("Enter Camera IP (a.b.c.d): ");
        _prompt = Prompt::ROUTING_SET_IP_VALUE;
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
        Serial.println("Camera IP set. Remember to Save Routing Table.");
        printRoutingMenu();
      } else {
        Serial.print("Invalid IP, try again: ");
        _prompt = Prompt::ROUTING_SET_IP_VALUE;
      }
      break;
    }
    case Prompt::ROUTING_CLEAR_IP_CAM: {
      int cam = line.toInt();
      if (cam >= 1 && cam <= CAMERA_SLOT_COUNT) {
        _routing.camera(cam)->ip.fromIPAddress(IPAddress(0, 0, 0, 0));
        Serial.println("Camera IP cleared. Remember to Save Routing Table.");
      } else {
        Serial.println("Invalid camera number.");
      }
      printRoutingMenu();
      break;
    }
    case Prompt::ROUTING_SET_PORT_CAM: {
      int cam = line.toInt();
      if (cam >= 1 && cam <= CAMERA_SLOT_COUNT) {
        _pendingCamNumber = (uint8_t)cam;
        Serial.print("Enter Camera Port: ");
        _prompt = Prompt::ROUTING_SET_PORT_VALUE;
      } else {
        Serial.println("Invalid camera number.");
        printRoutingMenu();
      }
      break;
    }
    case Prompt::ROUTING_SET_PORT_VALUE: {
      int port = line.toInt();
      if (port > 0 && port <= 65535) {
        _routing.camera(_pendingCamNumber)->port = (uint16_t)port;
        Serial.println("Camera port set. Remember to Save Routing Table.");
      } else {
        Serial.println("Invalid port.");
      }
      printRoutingMenu();
      break;
    }
    case Prompt::ROUTING_SET_PROTOCOL_CAM: {
      int cam = line.toInt();
      if (cam >= 1 && cam <= CAMERA_SLOT_COUNT) {
        _pendingCamNumber = (uint8_t)cam;
        Serial.println("1. IP_VISCA_RAW_UDP");
        Serial.println("2. IP_VISCA_RAW_TCP");
        Serial.println("3. SONY_VISCA_UDP");
        Serial.print("> ");
        _prompt = Prompt::ROUTING_SET_PROTOCOL_VALUE;
      } else {
        Serial.println("Invalid camera number.");
        printRoutingMenu();
      }
      break;
    }
    case Prompt::ROUTING_SET_PROTOCOL_VALUE: {
      int choice = line.toInt();
      if (choice >= 1 && choice <= 3) {
        _routing.camera(_pendingCamNumber)->protocol = (ProtocolMode)(choice - 1);
        Serial.println("Protocol set. Remember to Save Routing Table.");
      } else {
        Serial.println("Invalid choice.");
      }
      printRoutingMenu();
      break;
    }
    case Prompt::ROUTING_SET_ADDRMODE_CAM: {
      int cam = line.toInt();
      if (cam >= 1 && cam <= CAMERA_SLOT_COUNT) {
        _pendingCamNumber = (uint8_t)cam;
        Serial.println("1. rewrite_0x81");
        Serial.println("2. preserve");
        Serial.println("3. rewrite_by_cam");
        Serial.print("> ");
        _prompt = Prompt::ROUTING_SET_ADDRMODE_VALUE;
      } else {
        Serial.println("Invalid camera number.");
        printRoutingMenu();
      }
      break;
    }
    case Prompt::ROUTING_SET_ADDRMODE_VALUE: {
      int choice = line.toInt();
      if (choice >= 1 && choice <= 3) {
        _routing.camera(_pendingCamNumber)->addressMode = (AddressMode)(choice - 1);
        Serial.println("Address mode set. Remember to Save Routing Table.");
      } else {
        Serial.println("Invalid choice.");
      }
      printRoutingMenu();
      break;
    }
    case Prompt::NONE:
    default:
      break;
  }
}
