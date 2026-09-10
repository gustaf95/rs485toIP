#include "ConfigBackup.h"

#include <stdio.h>

#include "BoardProfile.h"
#include "FirmwareVersion.h"
#include "Rs485PinValidation.h"

// 형식 표시의 값 부분. 여기 한 곳에만 적어두고 아래 첫 줄과 파서가 같이 쓴다.
#define CONFIG_BACKUP_FORMAT_VALUE "rs485-visca-gateway-config-1"

const char kConfigBackupFormatLine[] = "format=" CONFIG_BACKUP_FORMAT_VALUE;

namespace {

// ---------------------------------------------------------------------------
// 쓰기
// ---------------------------------------------------------------------------

// 값에 제어문자가 섞이면 "한 줄에 한 항목"이라는 전제가 깨진다. SSID/비밀번호는 웹 폼의
// 한 줄 입력과 Serial 한 줄 입력에서만 오므로 실제로 그런 값이 들어올 길은 사실상 없지만,
// 한번 만들어진 파일은 나중에 고칠 방법이 없어서 여기서 걸러 둔다.
void appendText(String& out, const char* key, const char* value) {
  out += key;
  out += '=';
  for (const char* p = value; *p != '\0'; p++) {
    if ((uint8_t)*p >= 0x20) out += *p;
  }
  out += '\n';
}

void appendNum(String& out, const char* key, uint32_t value) {
  out += key;
  out += '=';
  out += value;
  out += '\n';
}

void appendIp(String& out, const char* key, const StoredIp& ip) {
  out += key;
  out += '=';
  out += ip.toIPAddress().toString();
  out += '\n';
}

// "cam3.port" 꼴의 키를 만든다. String으로 조립하면 카메라 일곱 개 × 다섯 줄마다 임시
// 객체가 생기므로 스택 버퍼에 찍는다.
void camKey(char* buf, size_t size, uint8_t cam, const char* field) {
  snprintf(buf, size, "cam%u.%s", (unsigned)cam, field);
}

// ---------------------------------------------------------------------------
// 읽기
// ---------------------------------------------------------------------------

// String::toInt()는 "abc"에도 조용히 0을 돌려준다. 이 파일은 손으로 고칠 수 있으니 숫자가
// 아닌 값을 0으로 받아들이면 안 된다 - 오타 하나가 그대로 핀 번호 0이 되어 버린다.
bool parseUint(const String& value, uint32_t max, uint32_t* out) {
  if (value.length() == 0 || value.length() > 10) return false;
  uint32_t n = 0;
  for (unsigned int i = 0; i < value.length(); i++) {
    const char c = value[i];
    if (c < '0' || c > '9') return false;
    n = n * 10 + (uint32_t)(c - '0');
  }
  if (n > max) return false;
  *out = n;
  return true;
}

// 1/0 외에 true/false, yes/no, on/off도 받는다 - 손으로 고친 파일에서 흔히 쓰는 표기라,
// 거부하는 것보다 받아주는 쪽이 사고가 적다.
bool parseBool(const String& value, bool* out) {
  String v = value;
  v.toLowerCase();
  if (v == "1" || v == "true" || v == "yes" || v == "on") {
    *out = true;
    return true;
  }
  if (v == "0" || v == "false" || v == "no" || v == "off") {
    *out = false;
    return true;
  }
  return false;
}

bool parseIp(const String& value, StoredIp* out) {
  IPAddress ip;
  if (!ip.fromString(value)) return false;
  out->fromIPAddress(ip);
  return true;
}

// char[] 필드에 넣는다. **잘라 넣지 않고 거부한다** - 잘린 SSID나 비밀번호는 조용히
// 접속만 안 되는 값이라, 화면에 "너무 길다"고 나오는 편이 훨씬 낫다.
bool assignText(char* dst, size_t size, const String& value) {
  if (value.length() >= size) return false;
  value.toCharArray(dst, size);
  return true;
}

bool isSupportedBaud(uint32_t baud) {
  for (uint8_t i = 0; i < RS485_BAUD_CHOICE_COUNT; i++) {
    if (RS485_BAUD_CHOICES[i] == baud) return true;
  }
  return false;
}

// "cam3.ip" -> 3을 반환하고 fieldOut에 "ip"를 담는다. 카메라 키가 아니면 0.
uint8_t parseCamKey(const String& key, String* fieldOut) {
  if (!key.startsWith("cam")) return 0;
  const int dot = key.indexOf('.');
  if (dot < 4) return 0;  // "cam" 뒤에 번호가 최소 한 글자는 있어야 한다
  uint32_t n = 0;
  if (!parseUint(key.substring(3, dot), CAMERA_SLOT_COUNT, &n) || n == 0) return 0;
  *fieldOut = key.substring(dot + 1);
  return (uint8_t)n;
}

// 건너뛴 줄 목록의 상한. 설정 파일이 아닌 것을 올리면(예: firmware.bin) 거의 모든 줄이
// 걸리는데, 제한이 없으면 수천 줄이 힙에 쌓이고 화면도 읽을 수 없게 된다.
const uint16_t kMaxNotes = 20;

}  // namespace

// ---------------------------------------------------------------------------

void serializeConfig(const SystemConfig& cfg, String& out) {
  // 완성된 파일이 2KB 남짓이다. 미리 잡아두면 += 하는 동안 재할당이 일어나지 않는다.
  out.reserve(out.length() + 2560);

  out += kConfigBackupFormatLine;
  out += '\n';
  out += "#\n";
  out += "# RS485 VISCA Gateway settings\n";
  out += "#   device   : ";
  out += cfg.wifi.apSsid;
  out += "\n#   board    : " BOARD_NAME "\n";
  out += "#   firmware : ";
  out += firmwareVersion();
  out += "\n#   built    : ";
  out += firmwareBuildTime();
  out += "\n#\n";
  out += "# Upload this file on the gateway's Backup page to load it back. Lines that\n";
  out += "# start with # are ignored, and spaces around = are fine.\n";
  out += "#\n";
  out += "# A key that is missing - or left with an empty value - keeps whatever the\n";
  out += "# gateway already has. That is how you push one file to several gateways:\n";
  out += "# delete the lines that have to stay different on each one.\n";
  out += "#\n";
  out += "# WARNING: the two password lines below are in the clear. Anyone who gets\n";
  out += "# this file has your Wi-Fi password.\n";
  out += "\n";

  out += "# ---- Wi-Fi (station) ----\n";
  appendText(out, "wifi.ssid", cfg.wifi.ssid);
  appendText(out, "wifi.password", cfg.wifi.password);
  appendNum(out, "wifi.dhcp", cfg.wifi.useDhcp ? 1 : 0);
  out += "# The three below are used only when wifi.dhcp=0, and have to differ per\n";
  out += "# gateway - delete them when copying this file to another one.\n";
  appendIp(out, "wifi.static_ip", cfg.wifi.staticIp);
  appendIp(out, "wifi.gateway", cfg.wifi.gateway);
  appendIp(out, "wifi.subnet", cfg.wifi.subnet);

  out += "\n# ---- Access point ----\n";
  out += "# The AP name ends in this board's MAC digits, so it is unique per gateway.\n";
  out += "# Delete these two lines when copying this file to another one.\n";
  appendText(out, "wifi.ap_ssid", cfg.wifi.apSsid);
  appendText(out, "wifi.ap_password", cfg.wifi.apPassword);

  out += "\n# ---- RS485 ----\n";
  out += "# baud: 2400 / 4800 / 9600 / 38400 / 115200\n";
  appendNum(out, "rs485.baud", cfg.rs485Baudrate);
  out += "# GPIO numbers are checked against the board that reads this file. A set that\n";
  out += "# does not fit is rejected as a whole and that board keeps its own pins -\n";
  out += "# which is what happens when a file moves between the two board types.\n";
  appendNum(out, "rs485.rx_pin", cfg.rs485RxPin);
  appendNum(out, "rs485.tx_pin", cfg.rs485TxPin);
  appendNum(out, "rs485.de_re_pin", cfg.rs485DeRePin);
  out += "# invert: 1 corrects A/B (D+/D-) wired the wrong way round\n";
  appendNum(out, "rs485.invert", cfg.rs485Invert ? 1 : 0);
  out += "# input_protocol: 0=VISCA 1=Pelco-D 2=Pelco-P 3=Pelco-D/P auto\n";
  appendNum(out, "rs485.input_protocol", (uint32_t)cfg.inputProtocol);
  out += "# pelco_response: 0=synthetic ACK 1=none\n";
  appendNum(out, "rs485.pelco_response", (uint32_t)cfg.pelcoResponseMode);
  out += "# response_mode: 0=none 1=synthetic 2=forward 3=forward+rewrite\n";
  appendNum(out, "rs485.response_mode", (uint32_t)cfg.responseMode);

  out += "\n# ---- Status LED ----\n";
  appendNum(out, "led.pin", cfg.statusLedPin);
  out += "# active_low: 1 = LOW lights the LED (the C3 Super Mini's on-board one)\n";
  appendNum(out, "led.active_low", cfg.statusLedActiveLow ? 1 : 0);

  out += "\n# ---- Debug ----\n";
  appendNum(out, "debug", cfg.debugMode ? 1 : 0);

  out += "\n# ---- Cameras ----\n";
  out += "# ip=0.0.0.0 leaves the slot unused.\n";
  out += "# protocol: 0=IP VISCA raw/UDP 1=IP VISCA raw/TCP 2=Sony VISCA over IP/UDP\n";
  out += "# address_mode: 0=rewrite to 0x81 1=preserve 2=rewrite by camera number\n";
  out += "# auto_power: 1 = follow RS485 bus activity (power on / standby)\n";

  char key[24];
  for (uint8_t n = 1; n <= CAMERA_SLOT_COUNT; n++) {
    const CameraSlot& slot = cfg.cameras[n - 1];
    out += '\n';
    camKey(key, sizeof(key), n, "ip");
    appendIp(out, key, slot.ip);
    camKey(key, sizeof(key), n, "port");
    appendNum(out, key, slot.port);
    camKey(key, sizeof(key), n, "protocol");
    appendNum(out, key, (uint32_t)slot.protocol);
    camKey(key, sizeof(key), n, "address_mode");
    appendNum(out, key, (uint32_t)slot.addressMode);
    camKey(key, sizeof(key), n, "auto_power");
    appendNum(out, key, slot.autoPowerControl ? 1 : 0);
  }
}

// ---------------------------------------------------------------------------

ConfigRestoreResult restoreConfig(const String& text, SystemConfig& cfg) {
  ConfigRestoreResult result;

  // 사본에 채운 뒤 마지막에 한 번 옮긴다 - 파일이 통째로 거부되는 경우에 반쯤 적용된
  // 설정이 남지 않게 하기 위함이다.
  SystemConfig work = cfg;

  uint16_t noteCount = 0;
  uint16_t notesDropped = 0;
  auto note = [&](const String& line) {
    if (noteCount >= kMaxNotes) {
      notesDropped++;
      return;
    }
    if (result.notes.length() > 0) result.notes += '\n';
    result.notes += line;
    noteCount++;
  };

  bool sawFormat = false;
  // 핀 넷은 서로를 보고 판정하므로(같은 핀을 두 곳에 쓸 수 없다) 줄마다 검사하지 못하고
  // 파일을 다 읽은 뒤에 한꺼번에 본다.
  bool pinsTouched = false;
  uint16_t pinKeysApplied = 0;

  int pos = 0;
  const int len = (int)text.length();
  uint16_t lineNo = 0;

  while (pos < len) {
    int nl = text.indexOf('\n', pos);
    if (nl < 0) nl = len;
    String line = text.substring(pos, nl);
    pos = nl + 1;
    lineNo++;

    line.trim();  // 앞뒤 공백과 CRLF의 '\r'을 같이 걷어낸다
    if (line.length() == 0 || line[0] == '#') continue;

    const int eq = line.indexOf('=');
    if (eq < 0) {
      result.skipped++;
      note("line " + String(lineNo) + ": no '=' on this line.");
      continue;
    }

    String key = line.substring(0, eq);
    key.trim();
    key.toLowerCase();
    String value = line.substring(eq + 1);
    value.trim();

    if (key == "format") {
      // 값까지 확인한다. 형식이 다른 파일을 "format 줄이 있으니 우리 것"이라고 받아주면
      // 엉뚱한 값이 절반쯤 들어간다.
      if (value == CONFIG_BACKUP_FORMAT_VALUE) sawFormat = true;
      continue;
    }

    // **빈 값은 "지금 값 유지"다.** 파일에서 그 줄을 지운 것과 같게 다룬다 - 웹 폼의
    // "leave blank to keep current"와도 같은 규칙이라 헷갈릴 여지가 없다.
    if (value.length() == 0) continue;

    const String where = "line " + String(lineNo) + " (" + key + "): ";
    uint32_t num = 0;
    bool flag = false;

    // ---- Wi-Fi ----
    if (key == "wifi.ssid") {
      if (assignText(work.wifi.ssid, sizeof(work.wifi.ssid), value)) {
        result.applied++;
      } else {
        result.skipped++;
        note(where + "SSID is longer than " + String(sizeof(work.wifi.ssid) - 1) +
             " characters.");
      }
    } else if (key == "wifi.password") {
      if (assignText(work.wifi.password, sizeof(work.wifi.password), value)) {
        result.applied++;
      } else {
        result.skipped++;
        note(where + "password is longer than " + String(sizeof(work.wifi.password) - 1) +
             " characters.");
      }
    } else if (key == "wifi.dhcp") {
      if (parseBool(value, &flag)) {
        work.wifi.useDhcp = flag;
        result.applied++;
      } else {
        result.skipped++;
        note(where + "expected 1 or 0.");
      }
    } else if (key == "wifi.static_ip" || key == "wifi.gateway" || key == "wifi.subnet") {
      StoredIp* target = key == "wifi.static_ip" ? &work.wifi.staticIp
                         : key == "wifi.gateway" ? &work.wifi.gateway
                                                 : &work.wifi.subnet;
      if (parseIp(value, target)) {
        result.applied++;
      } else {
        result.skipped++;
        note(where + "not an IPv4 address.");
      }
    } else if (key == "wifi.ap_ssid") {
      if (assignText(work.wifi.apSsid, sizeof(work.wifi.apSsid), value)) {
        result.applied++;
      } else {
        result.skipped++;
        note(where + "AP name is longer than " + String(sizeof(work.wifi.apSsid) - 1) +
             " characters.");
      }
    } else if (key == "wifi.ap_password") {
      // WPA2는 8자 미만을 받지 않는다. 그대로 넣으면 softAP()가 실패해서 **AP 자체가 안
      // 뜬다** - 웹이 유일한 접근 경로인 기기에서는 그게 곧 잠김이라 파일 단계에서 막는다.
      if (value.length() < 8) {
        result.skipped++;
        note(where +
             "AP password must be at least 8 characters (WPA2) or the AP will not start.");
      } else if (assignText(work.wifi.apPassword, sizeof(work.wifi.apPassword), value)) {
        result.applied++;
      } else {
        result.skipped++;
        note(where + "AP password is longer than " +
             String(sizeof(work.wifi.apPassword) - 1) + " characters.");
      }

      // ---- RS485 ----
    } else if (key == "rs485.baud") {
      if (parseUint(value, 1000000, &num) && isSupportedBaud(num)) {
        work.rs485Baudrate = num;
        result.applied++;
      } else {
        result.skipped++;
        note(where + "expected one of 2400, 4800, 9600, 38400, 115200.");
      }
    } else if (key == "rs485.rx_pin" || key == "rs485.tx_pin" || key == "rs485.de_re_pin" ||
               key == "led.pin") {
      if (parseUint(value, 255, &num)) {
        if (key == "rs485.rx_pin") {
          work.rs485RxPin = (uint8_t)num;
        } else if (key == "rs485.tx_pin") {
          work.rs485TxPin = (uint8_t)num;
        } else if (key == "rs485.de_re_pin") {
          work.rs485DeRePin = (uint8_t)num;
        } else {
          work.statusLedPin = (uint8_t)num;
        }
        pinsTouched = true;
        pinKeysApplied++;
        result.applied++;
      } else {
        result.skipped++;
        note(where + "expected a GPIO number.");
      }
    } else if (key == "rs485.invert") {
      if (parseBool(value, &flag)) {
        work.rs485Invert = flag;
        result.applied++;
      } else {
        result.skipped++;
        note(where + "expected 1 or 0.");
      }
    } else if (key == "rs485.input_protocol") {
      // 삭제된 Raw Bridge(4)가 파일로 되살아나지 않도록 상한을 둔다 (RoutingTable.h 참고).
      if (parseUint(value, (uint32_t)InputProtocol::PELCO_AUTO, &num)) {
        work.inputProtocol = (InputProtocol)num;
        result.applied++;
      } else {
        result.skipped++;
        note(where + "expected 0..3 (VISCA / Pelco-D / Pelco-P / Pelco auto).");
      }
    } else if (key == "rs485.pelco_response") {
      if (parseUint(value, (uint32_t)PelcoResponseMode::NONE, &num)) {
        work.pelcoResponseMode = (PelcoResponseMode)num;
        result.applied++;
      } else {
        result.skipped++;
        note(where + "expected 0 (synthetic ACK) or 1 (none).");
      }
    } else if (key == "rs485.response_mode") {
      if (parseUint(value, (uint32_t)ResponseMode::FORWARD_REWRITE, &num)) {
        work.responseMode = (ResponseMode)num;
        result.applied++;
      } else {
        result.skipped++;
        note(where + "expected 0..3.");
      }

      // ---- Status LED / Debug ----
    } else if (key == "led.active_low") {
      if (parseBool(value, &flag)) {
        work.statusLedActiveLow = flag;
        result.applied++;
      } else {
        result.skipped++;
        note(where + "expected 1 or 0.");
      }
    } else if (key == "debug") {
      if (parseBool(value, &flag)) {
        work.debugMode = flag;
        result.applied++;
      } else {
        result.skipped++;
        note(where + "expected 1 or 0.");
      }

      // ---- Cameras ----
    } else {
      String field;
      const uint8_t cam = parseCamKey(key, &field);
      if (cam == 0) {
        result.skipped++;
        note(where + "unknown setting.");
        continue;
      }
      CameraSlot& slot = work.cameras[cam - 1];

      if (field == "ip") {
        if (parseIp(value, &slot.ip)) {
          result.applied++;
        } else {
          result.skipped++;
          note(where + "not an IPv4 address (use 0.0.0.0 for an unused slot).");
        }
      } else if (field == "port") {
        if (parseUint(value, 65535, &num) && num > 0) {
          slot.port = (uint16_t)num;
          result.applied++;
        } else {
          result.skipped++;
          note(where + "expected a port between 1 and 65535.");
        }
      } else if (field == "protocol") {
        // 삭제된 RAW_DATA_UDP(3)를 걸러내려고 상한을 둔다 (RoutingTable.h 참고).
        if (parseUint(value, (uint32_t)ProtocolMode::SONY_VISCA_UDP, &num)) {
          slot.protocol = (ProtocolMode)num;
          result.applied++;
        } else {
          result.skipped++;
          note(where + "expected 0..2.");
        }
      } else if (field == "address_mode") {
        if (parseUint(value, (uint32_t)AddressMode::REWRITE_BY_CAM, &num)) {
          slot.addressMode = (AddressMode)num;
          result.applied++;
        } else {
          result.skipped++;
          note(where + "expected 0..2.");
        }
      } else if (field == "auto_power") {
        if (parseBool(value, &flag)) {
          slot.autoPowerControl = flag;
          result.applied++;
        } else {
          result.skipped++;
          note(where + "expected 1 or 0.");
        }
      } else {
        result.skipped++;
        note(where + "unknown camera setting.");
      }
    }
  }

  if (!sawFormat) {
    result.error = String("This is not a gateway settings file - the \"") +
                   kConfigBackupFormatLine + "\" line is missing. Nothing was changed.";
    return result;
  }

  // 핀 넷을 한꺼번에 검사하고, 하나라도 걸리면 **넷 다** 원래 값으로 되돌린다. 셋만 바뀐
  // 채로 남으면 파일에도 기기에도 없는 조합이 되어 어느 쪽을 봐도 지금 배선을 알 수 없다.
  //
  // 다른 보드에서 받은 파일이 여기 걸린다 - 클래식 ESP32와 C3는 쓸 수 있는 GPIO가 거의
  // 겹치지 않아서(3.1절), 핀만 빠지고 Wi-Fi/라우팅 등 나머지는 그대로 복원된다.
  if (pinsTouched) {
    String pinError;
    const bool pinsOk =
        validateStatusLedPin(work.statusLedPin, work.rs485RxPin, work.rs485TxPin,
                             work.rs485DeRePin, &pinError) &&
        validateRs485Pin(work.rs485RxPin, /*requireOutput=*/false, work.statusLedPin,
                         &pinError) &&
        validateRs485Pin(work.rs485TxPin, /*requireOutput=*/true, work.statusLedPin, &pinError) &&
        validateRs485Pin(work.rs485DeRePin, /*requireOutput=*/true, work.statusLedPin, &pinError);

    if (!pinsOk) {
      work.rs485RxPin = cfg.rs485RxPin;
      work.rs485TxPin = cfg.rs485TxPin;
      work.rs485DeRePin = cfg.rs485DeRePin;
      work.statusLedPin = cfg.statusLedPin;
      result.applied -= pinKeysApplied;
      result.skipped += pinKeysApplied;
      note("rs485.rx_pin / rs485.tx_pin / rs485.de_re_pin / led.pin: " + pinError +
           " The whole pin set was rejected, so this board keeps RX=" + String(cfg.rs485RxPin) +
           " TX=" + String(cfg.rs485TxPin) + " DE/RE=" + String(cfg.rs485DeRePin) +
           " LED=" + String(cfg.statusLedPin) + ".");
    }
  }

  if (notesDropped > 0) {
    result.notes += "\n... and " + String(notesDropped) + " more line(s) skipped.";
  }

  cfg = work;
  result.ok = true;
  return result;
}
