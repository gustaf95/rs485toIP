#include "DeviceRouter.h"
#include <string.h>

void DeviceRouter::buildOutputPacket(const TargetDevice& device, const uint8_t* input,
                                     uint8_t inputLen, RoutedPacket* result) {
  uint8_t len = (inputLen < VISCA_BUFFER_SIZE) ? inputLen : (uint8_t)VISCA_BUFFER_SIZE;
  memcpy(result->output, input, len);

  if (device.outputAddressMode == OutputAddressMode::REWRITE_TO_0x81 && len > 0) {
    result->output[0] = VISCA_ADDR_REWRITE_TARGET;
  }

  result->outputLen = len;
}

uint8_t DeviceRouter::route(const uint8_t* input, uint8_t inputLen, RoutedPacket* results,
                            uint8_t maxResults) {
  if (inputLen == 0 || maxResults == 0) {
    return 0;
  }

  uint8_t addressByte = input[0];
  SystemConfig& cfg = _config.get();

  if (addressByte == VISCA_ADDR_BROADCAST) {
    if (cfg.broadcastMode == BroadcastMode::IGNORE) {
      return 0;
    }

    uint8_t count = 0;
    for (uint8_t i = 0; i < cfg.deviceCount && count < maxResults; i++) {
      TargetDevice& dev = cfg.devices[i];
      if (!dev.enabled) continue;

      results[count].device = &dev;
      buildOutputPacket(dev, input, inputLen, &results[count]);
      count++;
    }
    return count;
  }

  TargetDevice* dev = _config.findByInputAddress(addressByte);
  if (dev == nullptr || !dev->enabled) {
    return 0;
  }

  results[0].device = dev;
  buildOutputPacket(*dev, input, inputLen, &results[0]);
  return 1;
}
