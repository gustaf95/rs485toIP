#pragma once

#include <Arduino.h>

// 보드의 GPIO 제약에 따라 RS485 RX/TX/DE-RE 핀과 상태 LED 핀으로 쓸 수 있는지 검사한다.
// SerialMenu와 WebConfigServer가 동일한 규칙을 쓰도록 공유한다 - 둘 중 하나만
// 검증 규칙을 갱신하면 두 UI가 서로 다른 핀을 허용/거부하게 되어버린다.
//
// 실제 핀 번호는 전부 BoardProfile.h에 있고, 어떤 핀이 왜 막히는지는 .cpp의 술어들에
// 있다. ESP32 클래식과 ESP32-C3는 예약 핀 목록이 거의 겹치지 않으므로(클래식:
// 1/3/6~11, C3: 11~17/18~19) 숫자를 다른 데 적어두면 보드를 바꿀 때 반드시 어긋난다.

// pin이 RS485 RX/TX/DE-RE로 쓰기에 유효하면 true를 반환한다. 유효하지 않으면
// false를 반환하고 *errorOut에 이유를 채운다. requireOutput은 TX/DE-RE처럼
// 출력이 필요한 핀인지 여부(RX 검사 시에는 false로 호출). statusLedPin은 현재
// 설정된 상태 LED 핀(cfg.statusLedPin) - 서로 겹치지 않게 막기 위해 필요하다.
bool validateRs485Pin(int pin, bool requireOutput, uint8_t statusLedPin, String* errorOut);

// pin이 상태 LED로 쓰기에 유효하면 true를 반환한다. 상태 LED는 항상 출력이라
// requireOutput 인자가 없다. rs485RxPin/TxPin/DeRePin은 현재 RS485 설정 - 서로
// 겹치지 않게 막기 위해 필요하다.
bool validateStatusLedPin(int pin, uint8_t rs485RxPin, uint8_t rs485TxPin, uint8_t rs485DeRePin,
                           String* errorOut);

// 위 두 함수가 **거부하지는 않지만** 알려야 하는 핀에 대한 경고 문구. 해당 없으면
// nullptr. 두 UI가 같은 문구를 쓰게 하는 것이 목적이다.
const char* gpioWarning(uint8_t pin);

// 세 핀을 한 번에 훑어 경고 종류를 비트마스크로 모은다. RS485 폼처럼 RX/TX/DE-RE를
// 한꺼번에 저장하고 리다이렉트로 결과를 알려야 하는 화면에서 쓴다 - 거기서는 문구가
// 아니라 "어떤 종류의 경고가 있었는가"만 넘기면 된다.
enum : uint8_t {
  GPIO_WARN_NONE = 0,
  GPIO_WARN_STRAPPING = 1 << 0,
  GPIO_WARN_BOOT_LOG = 1 << 1,
};
uint8_t gpioWarningFlags(uint8_t a, uint8_t b, uint8_t c);
