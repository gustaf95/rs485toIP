#include "RoutingTable.h"
#include <string.h>

void RoutingTable::applyDefaults() {
  memset(&_config, 0, sizeof(_config));

  _config.wifi.ssid[0] = '\0';
  _config.wifi.password[0] = '\0';
  _config.wifi.useDhcp = true;

  _config.rs485Baudrate = RS485_BAUD_DEFAULT;
  _config.rs485RxPin = RS485_RX_PIN_DEFAULT;
  _config.rs485TxPin = RS485_TX_PIN_DEFAULT;
  _config.rs485DeRePin = RS485_DE_RE_PIN_DEFAULT;

  _config.responseMode = ResponseMode::NONE;

  for (uint8_t i = 0; i < CAMERA_SLOT_COUNT; i++) {
    CameraSlot& slot = _config.cameras[i];
    slot.ip = {{0, 0, 0, 0}};
    slot.port = DEFAULT_CAMERA_PORT;
    slot.protocol = ProtocolMode::IP_VISCA_RAW_UDP;
    slot.addressMode = AddressMode::REWRITE_0x81;
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
