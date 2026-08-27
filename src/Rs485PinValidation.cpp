#include "Rs485PinValidation.h"

#include "BoardProfile.h"

namespace {
const int kMaxGpio = BOARD_MAX_GPIO;

// 아래 네 술어는 이 파일 안에서만 쓴다. 예전에는 헤더로 노출했는데, UI 쪽이 직접
// 물어보던 자리를 gpioWarning()/gpioWarningFlags()가 대신하면서 아무도 부르지 않게
// 됐다. 밖에 열어두면 두 UI가 또 제각각 조합해 쓰기 시작한다.

// 절대 사용 불가. 내장 SPI 플래시, USB D-/D+, (클래식의 경우) UART0 콘솔 핀.
bool isReservedGpio(uint8_t pin) {
  return BOARD_GPIO_IS_RESERVED(pin);
}

// 입력 전용 - 출력(TX, DE/RE, LED)으로는 사용 불가. C3에는 해당 핀이 없어 항상 false다.
bool isInputOnlyGpio(uint8_t pin) {
  return BOARD_GPIO_IS_INPUT_ONLY(pin);
}

// 부팅 모드를 결정하는 스트래핑 핀. 사용은 가능하나 외부 배선에 따라 부팅에 영향을
// 줄 수 있어 경고만 한다.
bool isStrappingGpio(uint8_t pin) {
  return BOARD_GPIO_IS_STRAPPING(pin);
}

// ROM 부트로더가 부팅 로그를 뿜는 핀(C3의 UART0 = GPIO20/21). 여기에 RS485
// 트랜시버를 물리면 리셋할 때마다 버스에 쓰레기 바이트가 실린다 - 역시 경고만 한다.
bool isBootLogGpio(uint8_t pin) {
  return BOARD_GPIO_IS_BOOT_LOG(pin);
}

// 범위 초과 메시지. 보드마다 상한이 달라(클래식 39, C3 21) 문자열을 그때그때
// 조립하지 않고 한 번만 만든다.
String rangeError() {
  return String("Invalid GPIO number (0-") + kMaxGpio + ") on " BOARD_NAME ".";
}
}  // namespace

const char* gpioWarning(uint8_t pin) {
  // 부팅 로그 쪽을 먼저 본다 - 스트래핑은 "배선에 따라 부팅이 막힐 수 있다"는 조건부
  // 경고인데, 부팅 로그는 RS485 버스가 매 리셋마다 확실히 오염된다는 무조건적인
  // 이야기라 더 급하다.
  if (isBootLogGpio(pin)) {
    return "GPIO is the ROM bootloader's log output (UART0) - every reset dumps boot "
           "messages onto this pin. Keep the RS485 driver disabled at boot (pull DE/RE low) "
           "or pick another pin.";
  }
  if (isStrappingGpio(pin)) {
    return "GPIO is a boot strapping pin - verify no external pull affects boot.";
  }
  return nullptr;
}

uint8_t gpioWarningFlags(uint8_t a, uint8_t b, uint8_t c) {
  uint8_t flags = GPIO_WARN_NONE;
  const uint8_t pins[3] = {a, b, c};
  for (uint8_t pin : pins) {
    if (isStrappingGpio(pin)) flags |= GPIO_WARN_STRAPPING;
    if (isBootLogGpio(pin)) flags |= GPIO_WARN_BOOT_LOG;
  }
  return flags;
}

bool validateRs485Pin(int pin, bool requireOutput, uint8_t statusLedPin, String* errorOut) {
  if (pin < 0 || pin > kMaxGpio) {
    *errorOut = rangeError();
    return false;
  }
  if (pin == statusLedPin) {
    *errorOut = "GPIO" + String(pin) + " is reserved for the status LED.";
    return false;
  }
  if (isReservedGpio((uint8_t)pin)) {
    *errorOut = "GPIO" + String(pin) + " is reserved (" BOARD_GPIO_RESERVED_REASON ").";
    return false;
  }
  if (requireOutput && isInputOnlyGpio((uint8_t)pin)) {
    *errorOut = "GPIO" + String(pin) + " is input-only; cannot be used here.";
    return false;
  }
  return true;
}

bool validateStatusLedPin(int pin, uint8_t rs485RxPin, uint8_t rs485TxPin, uint8_t rs485DeRePin,
                           String* errorOut) {
  if (pin < 0 || pin > kMaxGpio) {
    *errorOut = rangeError();
    return false;
  }
  if (pin == rs485RxPin || pin == rs485TxPin || pin == rs485DeRePin) {
    *errorOut = "GPIO" + String(pin) + " is reserved for RS485.";
    return false;
  }
  if (isReservedGpio((uint8_t)pin)) {
    *errorOut = "GPIO" + String(pin) + " is reserved (" BOARD_GPIO_RESERVED_REASON ").";
    return false;
  }
  if (isInputOnlyGpio((uint8_t)pin)) {
    *errorOut = "GPIO" + String(pin) + " is input-only; cannot drive an LED.";
    return false;
  }
  return true;
}
