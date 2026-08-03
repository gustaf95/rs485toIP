#include "RoutingTable.h"
#include <string.h>

void RoutingTable::applyDefaults() {
  memset(&_config, 0, sizeof(_config));

  _config.wifi.ssid[0] = '\0';
  _config.wifi.password[0] = '\0';
  _config.wifi.useDhcp = true;

  // apSsid는 일부러 비워둔다 - WebConfigServer::begin()이 MAC 기반 기본값을 한 번
  // 만들어서 여기에 채워 넣는다(RoutingTable.h 주석 참고). apPassword는 MAC 같은
  // 런타임 의존성이 없어서 지금 바로 고정 기본값을 채워도 된다.
  _config.wifi.apSsid[0] = '\0';
  strncpy(_config.wifi.apPassword, AP_PASSWORD_DEFAULT, sizeof(_config.wifi.apPassword) - 1);
  _config.wifi.apPassword[sizeof(_config.wifi.apPassword) - 1] = '\0';

  _config.rs485Baudrate = RS485_BAUD_DEFAULT;
  _config.rs485RxPin = RS485_RX_PIN_DEFAULT;
  _config.rs485TxPin = RS485_TX_PIN_DEFAULT;
  _config.rs485DeRePin = RS485_DE_RE_PIN_DEFAULT;
  _config.rs485Invert = RS485_INVERT_DEFAULT;
  _config.rs485Uart0Shared = false;
  _config.statusLedPin = STATUS_LED_PIN_DEFAULT;
  _config.inputProtocol = InputProtocol::PELCO_D;
  // 기본값은 NONE(응답 안 함)이다. 이 게이트웨이가 놓이는 RS485 버스에는 컨트롤러가
  // 직접 제어하는 실물 카메라(EDIS ED-P 등)가 같이 물려 있고, 그 카메라들은 자기
  // 명령에 스스로 응답한다. 게이트웨이가 주소를 가리지 않고 ACK를 쏘면 실물 카메라의
  // 응답과 같은 버스에서 충돌해 컨트롤러가 양쪽 다 못 읽는다 - 응답이 필요 없는데
  // 보내는 쪽이 훨씬 치명적이다.
  _config.pelcoResponseMode = PelcoResponseMode::NONE;

  _config.responseMode = ResponseMode::NONE;
  _config.debugMode = false;

  for (uint8_t i = 0; i < CAMERA_SLOT_COUNT; i++) {
    CameraSlot& slot = _config.cameras[i];
    slot.ip = {{0, 0, 0, 0}};
    slot.port = DEFAULT_CAMERA_PORT;
    slot.protocol = ProtocolMode::IP_VISCA_RAW_UDP;
    slot.addressMode = AddressMode::PRESERVE;
    slot.autoPowerControl = false;
  }
}

CameraSlot* RoutingTable::camera(uint8_t camNumber) {
  if (camNumber < 1 || camNumber > CAMERA_SLOT_COUNT) {
    return nullptr;
  }
  return &_config.cameras[camNumber - 1];
}

void RoutingTable::buildOutputPacket(const CameraSlot& slot, uint8_t camNumber,
                                     const uint8_t* input, uint8_t inputLen,
                                     RoutedPacket* result) {
  uint8_t len = (inputLen < VISCA_BUFFER_SIZE) ? inputLen : (uint8_t)VISCA_BUFFER_SIZE;
  memcpy(result->output, input, len);

  if (len > 0) {
    switch (slot.addressMode) {
      case AddressMode::REWRITE_0x81:
        result->output[0] = VISCA_ADDR_REWRITE_TARGET;
        break;
      case AddressMode::REWRITE_BY_CAM:
        result->output[0] = 0x80 | camNumber;
        break;
      case AddressMode::PRESERVE:
      default:
        break;
    }
  }

  result->outputLen = len;
}

uint8_t RoutingTable::route(const uint8_t* input, uint8_t inputLen, RoutedPacket* results,
                            uint8_t maxResults) {
  if (inputLen == 0 || maxResults == 0) {
    return 0;
  }

  uint8_t addressByte = input[0];

  if (addressByte == VISCA_ADDR_BROADCAST) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < CAMERA_SLOT_COUNT && count < maxResults; i++) {
      CameraSlot& slot = _config.cameras[i];
      if (!slot.isConfigured()) continue;

      uint8_t camNumber = i + 1;
      results[count].slot = &slot;
      results[count].camNumber = camNumber;
      buildOutputPacket(slot, camNumber, input, inputLen, &results[count]);
      count++;
    }
    return count;
  }

  if (addressByte < VISCA_ADDR_CAM1 || addressByte > VISCA_ADDR_CAM7) {
    return 0;
  }

  uint8_t camNumber = addressByte - VISCA_ADDR_CAM1 + 1;
  CameraSlot* slot = camera(camNumber);
  if (slot == nullptr || !slot->isConfigured()) {
    return 0;
  }

  results[0].slot = slot;
  results[0].camNumber = camNumber;
  buildOutputPacket(*slot, camNumber, input, inputLen, &results[0]);
  return 1;
}
