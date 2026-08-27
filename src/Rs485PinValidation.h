#pragma once

#include <Arduino.h>

// 보드의 GPIO 제약에 따라 RS485 RX/TX/DE-RE 핀과 상태 LED 핀으로 쓸 수 있는지 검사한다.
// SerialMenu와 WebConfigServer가 동일한 규칙을 쓰도록 공유한다 - 둘 중 하나만
// 검증 규칙을 갱신하면 두 UI가 서로 다른 핀을 허용/거부하게 되어버린다.
//
// 실제 핀 번호는 전부 BoardProfile.h에 있고 여기서는 그 술어를 함수로 감싸기만 한다.
// ESP32 클래식과 ESP32-C3는 예약 핀 목록이 거의 겹치지 않으므로(클래식: 1/3/6~11,
// C3: 11~17/18~19) 숫자를 이 파일에 적어두면 보드를 바꿀 때 반드시 어긋난다.

// 절대 사용 불가. 내장 SPI 플래시, USB D-/D+, (클래식의 경우) UART0 콘솔 핀.
bool isReservedGpio(uint8_t pin);

// 입력 전용 - 출력(TX, DE/RE, LED)으로는 사용 불가. C3에는 해당 핀이 없어 항상 false다.
bool isInputOnlyGpio(uint8_t pin);

// 부팅 모드를 결정하는 스트래핑 핀. 사용은 가능하나 외부 배선에 따라 부팅에 영향을
// 줄 수 있어 경고만 표시한다 (validateRs485Pin은 이 핀을 거부하지 않음).
bool isStrappingGpio(uint8_t pin);

// ROM 부트로더가 부팅 로그를 뿜는 핀(C3의 UART0 = GPIO20/21). 여기에 RS485
// 트랜시버를 물리면 리셋할 때마다 버스에 쓰레기 바이트가 실린다 - 역시 경고만 한다.
bool isBootLogGpio(uint8_t pin);

// 위 두 경고 조건에 해당하면 사람이 읽을 수 있는 사유를, 아니면 nullptr을 반환한다.
// 두 UI가 같은 문구를 쓰도록 하고, SerialMenu의 핀 입력 처리 네 벌이 각자 경고를
// 찍던 중복을 없앤다.
const char* gpioWarning(uint8_t pin);

// 세 핀을 한 번에 훑어 경고 종류를 비트마스크로 모은다. RS485 폼처럼 RX/TX/DE-RE를
// 한꺼번에 저장하는 화면에서 "어떤 경고를 띄울지"를 정하는 데 쓴다.
enum : uint8_t {
  GPIO_WARN_NONE = 0,
  GPIO_WARN_STRAPPING = 1 << 0,
  GPIO_WARN_BOOT_LOG = 1 << 1,
};
uint8_t gpioWarningFlags(uint8_t a, uint8_t b, uint8_t c);

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
