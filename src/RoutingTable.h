#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include "config.h"

enum class ProtocolMode : uint8_t {
  IP_VISCA_RAW_UDP = 0,
  IP_VISCA_RAW_TCP = 1,
  SONY_VISCA_UDP = 2
};

enum class AddressMode : uint8_t {
  REWRITE_0x81 = 0,
  PRESERVE = 1,
  REWRITE_BY_CAM = 2
};

enum class ResponseMode : uint8_t {
  NONE = 0,
  SYNTHETIC = 1,
  FORWARD = 2,
  FORWARD_REWRITE = 3
};

// RS485로 들어오는 입력이 어느 프로토콜인지. Pelco-D/Pelco-P는 아직 프레이밍/
// 체크섬 검증과 ACK 응답까지만 구현되어 있고, VISCA로의 명령 변환은 별도
// 작업이다.
//
// PELCO_AUTO는 Pelco-D와 Pelco-P를 패킷 단위로 실시간 자동 판별한다. 이게
// 가능한 이유는 두 프로토콜의 시작 바이트가 겹치지 않기 때문이다(Pelco-D는
// 0xFF, Pelco-P는 0xA0). ZU-EPC7000 컨트롤러는 카메라 채널마다 프로토콜을
// 개별 지정할 수 있어(doc/pelcoD_command.md 9.1/9.3절) 같은 RS485 버스에
// Pelco-D와 Pelco-P가 실제로 섞여 들어올 수 있다는 게 확인되어 추가했다.
// VISCA는 종료 바이트 0xFF가 Pelco-D의 시작 바이트와 겹쳐 안전하게 자동
// 판별할 수 없으므로(같은 문서 참고) 이 옵션에 포함하지 않는다 - VISCA는
// 항상 명시적으로 선택해야 한다.
enum class InputProtocol : uint8_t {
  VISCA = 0,
  PELCO_D = 1,
  PELCO_P = 2,
  PELCO_AUTO = 3
};

// Pelco-D/Pelco-P 입력을 받았을 때 RS485로 General Response(ACK)를 돌려줄지
// 여부. 두 프로토콜 공통 설정이다 - ZU-EPC7000 컨트롤러 매뉴얼의 "ACK MODE"
// 설정도 Pelco-D/Pelco-P를 구분하지 않고 "PELCO 공통"으로 적용된다.
// 기본값은 SYNTHETIC(응답함) - 컨트롤러가 응답을 기다리다 멈추는 쪽이 응답을
// 안 보내는 쪽보다 훨씬 치명적이라, 안전한 쪽을 기본값으로 삼는다.
enum class PelcoResponseMode : uint8_t {
  SYNTHETIC = 0,
  NONE = 1
};

// IPv4 address stored as raw bytes so the struct stays a flat, blob-safe
// layout for Preferences storage (avoids depending on IPAddress internals).
struct StoredIp {
  uint8_t bytes[4];

  IPAddress toIPAddress() const {
    return IPAddress(bytes[0], bytes[1], bytes[2], bytes[3]);
  }

  void fromIPAddress(const IPAddress& ip) {
    bytes[0] = ip[0];
    bytes[1] = ip[1];
    bytes[2] = ip[2];
    bytes[3] = ip[3];
  }

  bool isZero() const {
    return bytes[0] == 0 && bytes[1] == 0 && bytes[2] == 0 && bytes[3] == 0;
  }
};

// 카메라 1~7 고정 슬롯 하나의 설정. IP가 비어 있으면 해당 슬롯은 미사용으로 취급한다.
struct CameraSlot {
  StoredIp ip;
  uint16_t port;
  ProtocolMode protocol;
  AddressMode addressMode;

  bool isConfigured() const { return !ip.isZero(); }
};

struct WifiConfig {
  char ssid[32];
  char password[64];
  bool useDhcp;
  StoredIp staticIp;
  StoredIp gateway;
  StoredIp subnet;
};

struct SystemConfig {
  uint32_t magic;
  uint16_t version;

  WifiConfig wifi;

  uint32_t rs485Baudrate;
  uint8_t rs485RxPin;
  uint8_t rs485TxPin;
  uint8_t rs485DeRePin;
  InputProtocol inputProtocol;
  PelcoResponseMode pelcoResponseMode;

  ResponseMode responseMode;
  bool debugMode;

  CameraSlot cameras[CAMERA_SLOT_COUNT];  // index 0 = CAM1 ... index 6 = CAM7
};

struct RoutedPacket {
  CameraSlot* slot;
  uint8_t camNumber;  // 1~7
  uint8_t output[VISCA_BUFFER_SIZE];
  uint8_t outputLen;
};

// 카메라 1~7 고정 슬롯 설정을 보관하고, RS485에서 받은 VISCA 패킷의 첫 바이트를
// 보고 어느 카메라로 보낼지, 주소 바이트를 어떻게 재작성할지 결정한다.
class RoutingTable {
 public:
  void applyDefaults();

  SystemConfig& get() { return _config; }
  const SystemConfig& get() const { return _config; }

  // camNumber는 1~7. 범위를 벗어나면 nullptr.
  CameraSlot* camera(uint8_t camNumber);

  // 매칭된 대상 수를 반환한다 (0이면 무시됨: 카메라 번호에 IP 미설정, 또는
  // broadcast인데 IP가 설정된 카메라가 하나도 없는 경우).
  uint8_t route(const uint8_t* input, uint8_t inputLen, RoutedPacket* results, uint8_t maxResults);

 private:
  SystemConfig _config;

  void buildOutputPacket(const CameraSlot& slot, uint8_t camNumber, const uint8_t* input,
                         uint8_t inputLen, RoutedPacket* result);
};
