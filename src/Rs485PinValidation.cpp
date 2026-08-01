#include "Rs485PinValidation.h"
#include "config.h"

namespace {
const int kMaxGpio = 39;
}  // namespace

bool isReservedGpio(uint8_t pin) {
  return pin == 1 || pin == 3 || (pin >= 6 && pin <= 11);
}

bool isInputOnlyGpio(uint8_t pin) {
  return pin >= 34 && pin <= 39;
}

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
