#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include "config.h"

// RAW_DATA_UDP는 sendToCamera()의 일반 VISCA 라우팅 경로로는 쓰이지 않는다 -
// InputProtocol::RAW_BRIDGE 모드에서 카메라 슬롯 하나를 "브릿지 피어"로 지정하는
// 용도로만 쓰이며, main.cpp의 findRawBridgePeer()가 이 값으로 슬롯을 찾는다.
enum class ProtocolMode : uint8_t {
  IP_VISCA_RAW_UDP = 0,
  IP_VISCA_RAW_TCP = 1,
  SONY_VISCA_UDP = 2,
  RAW_DATA_UDP = 3
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

// RS485로 들어오는 입력이 어느 프로토콜인지. Pelco-D/Pelco-P는 FoMaKo가 지원하는
// 범위의 명령을 VISCA로 변환해서 전달한다 (src/main.cpp translatePelcoAndForward()).
//
// PELCO_AUTO는 Pelco-D와 Pelco-P를 패킷 단위로 실시간 자동 판별한다. 이게
// 가능한 이유는 두 프로토콜의 시작 바이트가 겹치지 않기 때문이다(Pelco-D는
// 0xFF, Pelco-P는 0xA0). ZU-EPC7000 컨트롤러는 카메라 채널마다 프로토콜을
// 개별 지정할 수 있어(doc/pelcoD_command.md 9.1/9.3절) 같은 RS485 버스에
// Pelco-D와 Pelco-P가 실제로 섞여 들어올 수 있다는 게 확인되어 추가했다.
// VISCA는 종료 바이트 0xFF가 Pelco-D의 시작 바이트와 겹쳐 안전하게 자동
// 판별할 수 없으므로(같은 문서 참고) 이 옵션에 포함하지 않는다 - VISCA는
// 항상 명시적으로 선택해야 한다.
//
// RAW_BRIDGE는 VISCA/Pelco-D/Pelco-P 파싱을 전혀 하지 않는다 - RS485에 흐르는
// 바이트를 그대로 묶어서 카메라 슬롯 하나(Protocol=RAW_DATA_UDP로 지정된 슬롯)의
// IP:Port로 UDP 전송하고, 그 슬롯에서 받은 UDP 페이로드는 그대로 RS485 TX로
// 내보낸다. 이 firmware를 올린 게이트웨이 두 대를 마주 보게 설정하면(서로의 IP를
// 상대방 슬롯에 적어 넣으면) RS485 버스 하나를 IP망 너머로 그대로 연장하는
// 투명 브릿지가 된다. 두 대 다 카메라가 아니라 컨트롤러/카메라를 직접 상대하므로
// 프로토콜을 몰라도(심지어 VISCA/Pelco도 아닌 다른 RS485 프로토콜이어도) 동작한다.
enum class InputProtocol : uint8_t {
  VISCA = 0,
  PELCO_D = 1,
  PELCO_P = 2,
  PELCO_AUTO = 3,
  RAW_BRIDGE = 4
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
  // RS485 버스의 유효 패킷 유무로 이 카메라의 전원을 자동으로 켜고/대기시킬지
  // (main.cpp의 updateAutoPowerFromChatter()/pollAutoPowerUnknown() 참고). 기본값
  // false(끔) - 켜면 지금까지의 수동 전원 동작 대신 자동 제어가 그 카메라를 넘겨받는다.
  bool autoPowerControl;

  bool isConfigured() const { return !ip.isZero(); }
};

struct WifiConfig {
  char ssid[32];
  char password[64];
  bool useDhcp;
  StoredIp staticIp;
  StoredIp gateway;
  StoredIp subnet;

  // WebConfigServer의 AP(13절)용 설정. apSsid가 빈 문자열이면 WebConfigServer::begin()이
  // MAC 주소 뒷자리를 붙여 기기별로 구분되는 기본값을 한 번 만들어 여기에 저장한다 -
  // 그 뒤로는 항상 이 필드값을 그대로 쓴다. apPassword는 applyDefaults()가
  // AP_PASSWORD_DEFAULT로 미리 채워두므로 비어있을 일이 없다.
  char apSsid[32];
  char apPassword[64];
};

struct SystemConfig {
  uint32_t magic;
  uint16_t version;

  WifiConfig wifi;

  uint32_t rs485Baudrate;
  uint8_t rs485RxPin;
  uint8_t rs485TxPin;
  uint8_t rs485DeRePin;
  // true면 UART 신호(RX/TX 양쪽)를 반전시킨다 - RS485 A/B(D+/D-)가 뒤집혀 결선된
  // 배선을 소프트웨어로 보정하는 용도다. 하드웨어에서 A/B를 바로잡는 게 정석이지만,
  // 결선을 손댈 수 없는 현장에서는 이 옵션이 유일한 해법이라 설정으로 노출한다.
  // 기본값은 RS485_INVERT_DEFAULT(false, 정상 결선 가정).
  bool rs485Invert;
  // true면 RS485가 UART0(Serial)를 공유한다 - RX/TX/DE-RE가 config.h의
  // RS485_UART0_SHARED_* 값으로 고정되고, Serial 메뉴와 Serial 기반 디버그 로깅이
  // 비활성화된다 (SerialMenu::poll(), main.cpp의 debugMode 게이팅 참고). 이 모드에서는
  // Web Config Server가 유일한 설정 UI다.
  bool rs485Uart0Shared;
  uint8_t statusLedPin;
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
