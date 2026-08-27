#pragma once

#include <Arduino.h>

// 보드마다 다른 하드웨어 사실을 한 곳에 모은다. 여기 없는 곳에서 GPIO 번호나 UART
// 번호를 직접 쓰면 보드를 바꿀 때 반드시 하나를 빠뜨린다 - 실제로 ESP32-C3 포팅에서
// 가장 위험했던 게 그런 종류의 누락이었다(아래 UART 번호 주석 참고).
//
// 지원 대상:
//   - ESP32 클래식 (esp32dev)          : 기존 설치 장비
//   - ESP32-C3 Super Mini (esp32c3_supermini) : 신규 기본 타겟
//
// 새 보드를 추가할 때 손댈 곳은 이 파일 하나여야 한다.

#if defined(CONFIG_IDF_TARGET_ESP32C3)

// ---------------------------------------------------------------------------
// ESP32-C3 Super Mini
// ---------------------------------------------------------------------------
#define BOARD_NAME "ESP32-C3 Super Mini"

// **C3의 UART는 0번과 1번 두 개뿐이다.** ESP32 클래식용 코드가 쓰던 UART2를 그대로
// 두면 컴파일도 되고 부팅도 되는데 RS485만 조용히 죽는다 - arduino-esp32의
// HardwareSerial::begin()이 `_uart_nr >= SOC_UART_NUM`일 때 예외를 던지지 않고
// log_e() 한 줄 남기고 그냥 return하기 때문이다(기본 로그 레벨에서는 그마저 안 보인다).
// 배선을 의심하며 몇 시간 날리기 딱 좋은 실패라, 이 상수를 반드시 거쳐 가게 한다.
//
// USB 콘솔이 네이티브 USB(CDC)로 빠지므로 UART0도 비어 있지만, UART0를 쓰면 부팅
// 로그가 딸려 나온다(BOARD_GPIO_IS_BOOT_LOG 주석 참고). 그래서 1번을 쓴다.
#define BOARD_RS485_UART_NUM 1
#define BOARD_RS485_UART_LABEL "UART1 / Serial1"

// C3의 GPIO는 0~21이다. 22~25는 아예 존재하지 않는다.
#define BOARD_MAX_GPIO 21

// Super Mini에서 실제로 핀 헤더로 뽑혀 나오는 것은 GPIO0~10, 20, 21뿐이다. 11~17은
// 패키지 내장 SPI 플래시가 쓰고(C3FH4/FN4), 18/19는 USB D-/D+라 헤더에 없다.
// 아래 기본값은 그중 스트래핑(2/8/9)·USB(18/19)·UART0(20/21)를 모두 피한 값이다.
#define BOARD_RS485_RX_PIN_DEFAULT 4
#define BOARD_RS485_TX_PIN_DEFAULT 5
#define BOARD_RS485_DE_RE_PIN_DEFAULT 6

// 보드에 붙어 있는 파란 LED. 외부 부품 없이 바로 상태 표시가 되므로 기본값으로 삼는다.
//
// GPIO8은 스트래핑 핀이지만 이 배선에서는 안전하다 - LED가 3V3 -> LED -> GPIO8로
// 물려 있어 부팅 시점에 LED를 통해 HIGH로 끌려가고, 그게 마침 정상 부팅 조건이다.
// 대신 액티브 로우다(LOW일 때 켜짐). BOARD_STATUS_LED_ACTIVE_LOW_DEFAULT 참고.
#define BOARD_STATUS_LED_PIN_DEFAULT 8
#define BOARD_STATUS_LED_ACTIVE_LOW_DEFAULT true

// 패키지 내장 SPI 플래시(11~17)와 USB D-/D+(18/19). 둘 다 헤더에 나오지도 않지만,
// 사용자가 숫자를 직접 입력할 수 있는 UI라 막아둔다.
#define BOARD_GPIO_IS_FLASH(pin) ((pin) >= 11 && (pin) <= 17)
#define BOARD_GPIO_IS_USB(pin) ((pin) == 18 || (pin) == 19)
#define BOARD_GPIO_IS_RESERVED(pin) (BOARD_GPIO_IS_FLASH(pin) || BOARD_GPIO_IS_USB(pin))
#define BOARD_GPIO_RESERVED_REASON "internal SPI flash (11-17) or USB D-/D+ (18/19)"

// C3에는 입력 전용 핀이 없다 - 0~21 전부 입출력이 된다. 클래식의 GPIO34~39에
// 해당하는 제약이 사라진 것이라, 검사 자체를 없애지 않고 항상 false로 둔다
// (양쪽 보드가 같은 함수를 쓰게 하려면 이 편이 분기가 적다).
#define BOARD_GPIO_IS_INPUT_ONLY(pin) ((void)(pin), false)

// C3의 부팅 스트래핑 핀. 클래식(0/2/5/12/15)과 완전히 다르다.
#define BOARD_GPIO_IS_STRAPPING(pin) ((pin) == 2 || (pin) == 8 || (pin) == 9)

// **UART0 기본 핀. 여기에 RS485 트랜시버를 물리면 매 부팅마다 버스가 오염된다.**
// C3의 ROM 부트로더는 USB CDC로 콘솔을 뺐든 말든 GPIO21로 115200bps 부팅 로그를
// 그대로 뿜는다. 그게 RS485 드라이버에 들어가면, 이미 ZU-EPC7000이 마스터로 물려
// 돌아가는 버스에 리셋할 때마다 수백 바이트의 쓰레기가 실린다.
//
// 그래도 완전히 막지는 않는다 - 핀이 13개뿐인 보드라 마지막 두 개를 봉인하면
// 선택지가 너무 좁아지고, DE/RE에 외부 풀다운을 달아 드라이버를 꺼두면 실제로는
// 문제가 없다. 거부 대신 경고로 알린다.
#define BOARD_GPIO_IS_BOOT_LOG(pin) ((pin) == 20 || (pin) == 21)

#else

// ---------------------------------------------------------------------------
// ESP32 클래식 (esp32dev) - 이 프로젝트의 원래 타겟
// ---------------------------------------------------------------------------
#define BOARD_NAME "ESP32 (classic)"

#define BOARD_RS485_UART_NUM 2
#define BOARD_RS485_UART_LABEL "UART2 / Serial2"

#define BOARD_MAX_GPIO 39

#define BOARD_RS485_RX_PIN_DEFAULT 25
#define BOARD_RS485_TX_PIN_DEFAULT 26
#define BOARD_RS485_DE_RE_PIN_DEFAULT 27

// 클래식 DevKit의 온보드 LED는 보드마다 위치가 달라 기본값으로 삼기 어렵다.
// 원래 값(GPIO13, 외부 LED, 액티브 하이)을 그대로 유지한다.
#define BOARD_STATUS_LED_PIN_DEFAULT 13
#define BOARD_STATUS_LED_ACTIVE_LOW_DEFAULT false

#define BOARD_GPIO_IS_FLASH(pin) ((pin) >= 6 && (pin) <= 11)
// 클래식에는 USB 주변장치가 없어 BOARD_GPIO_IS_USB에 해당하는 것이 아예 없다.
// GPIO1/3은 UART0(USB Serial 콘솔) - 클래식은 USB 브리지 칩이 이 핀에 물려 있어
// 콘솔을 다른 데로 옮길 수 없다. C3와 달리 경고가 아니라 거부다.
#define BOARD_GPIO_IS_RESERVED(pin) ((pin) == 1 || (pin) == 3 || BOARD_GPIO_IS_FLASH(pin))
#define BOARD_GPIO_RESERVED_REASON "UART0 console (1/3) or internal SPI flash (6-11)"

#define BOARD_GPIO_IS_INPUT_ONLY(pin) ((pin) >= 34 && (pin) <= 39)
#define BOARD_GPIO_IS_STRAPPING(pin) \
  ((pin) == 0 || (pin) == 2 || (pin) == 5 || (pin) == 12 || (pin) == 15)
// 클래식은 부팅 로그도 UART0(1/3)로 나가는데 그 핀은 이미 위에서 거부된다.
#define BOARD_GPIO_IS_BOOT_LOG(pin) ((void)(pin), false)

#endif
