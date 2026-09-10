#pragma once

#include <Arduino.h>

#include "BoardProfile.h"

// ---- RS485 UART ----
// 어느 UART를 쓰는지, 기본 핀이 몇 번인지는 보드마다 다르다 - BoardProfile.h가
// 유일한 출처다. 여기서 숫자를 다시 적으면 보드를 바꿀 때 두 곳이 어긋난다.
#define RS485_RX_PIN_DEFAULT BOARD_RS485_RX_PIN_DEFAULT
#define RS485_TX_PIN_DEFAULT BOARD_RS485_TX_PIN_DEFAULT
#define RS485_DE_RE_PIN_DEFAULT BOARD_RS485_DE_RE_PIN_DEFAULT
#define RS485_BAUD_DEFAULT 9600

// RS485 보드레이트로 고를 수 있는 값. Serial 메뉴("1. Set Baudrate")와 웹 설정 화면의
// <select>가 이 목록을 그리고, 설정 파일 복원(ConfigBackup.cpp)도 이 목록에 있는 값만
// 받아준다.
//
// 세 곳이 같은 목록을 봐야 하는 이유는 복원 쪽에 있다. 목록에 없는 값(예: 19200)을
// 파일로 밀어 넣으면 그 뒤로 두 UI 어디에도 지금 값에 해당하는 항목이 없어서, 사용자가
// RS485 화면을 열어 다른 항목을 건드리고 저장하는 것만으로 보드레이트가 조용히 바뀐다.
// 그래서 목록에 없는 값은 복원 단계에서 거부하고, 목록 자체는 여기 한 곳에만 둔다.
constexpr uint32_t RS485_BAUD_CHOICES[] = {2400, 4800, 9600, 38400, 115200};
constexpr uint8_t RS485_BAUD_CHOICE_COUNT =
    sizeof(RS485_BAUD_CHOICES) / sizeof(RS485_BAUD_CHOICES[0]);

// UART 신호 반전 여부의 초기 기본값. A/B(D+/D-)가 뒤집혀 결선된 배선에서는 이걸 켜야
// 수신 바이트가 안 깨진다(0xFF 프레임이 0x00으로 읽히고 나머지는 한 비트씩 밀린 보수값이
// 된다) - 그런 배선이면 RS485 Settings 화면에서 켜야 한다 (RoutingTable::applyDefaults()
// 참고). 정상 결선(A/B 안 뒤집힘)이 기본 가정이라 false로 둔다.
#define RS485_INVERT_DEFAULT false

// USB 콘솔의 속도. RS485 속도와 무관하며, Debug Mode 로그가 loop()를 오래 붙잡지
// 않도록 최대한 빠르게 잡는다 (main.cpp setup()의 주석 참고). 이 값을 바꾸면
// platformio.ini의 monitor_speed도 같이 맞춰야 한다.
//
// **ESP32-C3에서는 이 값이 아무 의미가 없다.** 콘솔이 UART0가 아니라 네이티브 USB
// CDC라 보드레이트라는 개념 자체가 없고, 속도는 USB가 정한다. 그래도 상수를 지우지
// 않는 이유는 클래식 보드에서는 여전히 필요하고, Serial.begin()에 넘길 값이 하나는
// 있어야 하기 때문이다(CDC 쪽은 인자를 무시한다).
#define SERIAL_CONSOLE_BAUD 115200

// RS485 UART 수신 링버퍼 크기. 기본값(256)이면 Debug Mode 로그처럼 loop()를 잠시
// 붙잡는 작업 중에 들어온 바이트가 넘쳐 유실된다. 9600bps 기준 1024바이트면 약 1초의
// 정체를 견딘다.
#define RS485_RX_BUFFER_SIZE 1024

// ---- Status LED ----
// RS485 Settings 화면(Serial/Web)에서 런타임에 바꿀 수 있다 - 이 값은 초기 기본값일
// 뿐이다 (RoutingTable::applyDefaults() 참고).
//
// 극성도 설정 항목이다. ESP32-C3 Super Mini의 온보드 LED(GPIO8)가 액티브 로우라
// 그 보드의 기본값은 true인데, 외부 LED를 다는 경우는 보통 액티브 하이다 - 어느
// 쪽이 맞는지는 보드가 아니라 배선이 정하므로 컴파일 타임에 못 박지 않는다.
#define STATUS_LED_PIN_DEFAULT BOARD_STATUS_LED_PIN_DEFAULT
#define STATUS_LED_ACTIVE_LOW_DEFAULT BOARD_STATUS_LED_ACTIVE_LOW_DEFAULT

// ---- VISCA framing ----
#define VISCA_BUFFER_SIZE 128
#define VISCA_MIN_PACKET_LEN 3
#define VISCA_PACKET_TIMEOUT_MS 50
#define VISCA_TERMINATOR 0xFF

// Pelco -> VISCA 번역 결과가 직전과 완전히 동일할 때, 이 시간 안에 들어온 재전송은
// 카메라로 내보내지 않는다 (main.cpp의 isDuplicateViscaCommand()). Pelco 컨트롤러는
// 조이스틱을 물고 있는 동안 같은 프레임을 초당 수십 번 재전송하는데, VISCA
// Pan-tiltDrive는 Stop이 올 때까지 유지되는 래치 명령이라 그대로 흘리면 카메라 명령
// 큐만 밀린다. 방향/속도가 바뀌면 바이트가 달라져 즉시 통과하므로 반응성은 그대로다.
//
// **상대 조정(R/B Gain, ExpComp의 Up/Down)에는 적용하지 않는다.** 그쪽은 같은 바이트의
// 반복이 곧 "한 칸 더"라서, 억제하면 200ms에 한 칸씩만 통과해 키를 눌러도 화면이 안
// 움직이는 것처럼 보인다. forwardTranslatedVisca()의 isRelativeStep 인자 참고.
#define VISCA_DUPLICATE_SUPPRESS_MS 200

// ---- EDIS ED-P 벤더 확장 (Pelco-D) ----
// ZU-EPC7000 <-> EDIS ED-P 사이에서 실측한 벤더 고유 확장 명령이다. 표준 Pelco-D
// 확장 옵코드 표(0x03~0x6F) 밖의 값이라 표준 문서에는 없다. 둘 다 CMND1=0x00을 쓴다.
//
//   SET:   FF ADDR 00 C3 pp qq CK   -> VISCA `8x 01 04 pp qq FF` 와 대체로 1:1
//   GET:   FF ADDR 00 D3 pp ?? CK   -> pp가 조회할 항목
//   조회 응답: FF ADDR R1 D7 pp val CK
//   거절 응답: FF ADDR 00 66 00 01 CK   실행할 수 없는 SET에만
//
// SET의 pp/qq가 VISCA 명령 코드/값과 대체로 같다는 게 실측으로 확인됐다 (Power
// On/Standby = `00 02`/`00 03`, Iris Auto/Manual = `39 00`/`39 03`, Focus Auto/Manual =
// `38 02`/`38 03`, Back Light On/Off = `33 02`/`33 03`). **성공한** SET에는 응답이 없고,
// 컨트롤러는 주기적인 GET 폴링으로 화면을 갱신한다. 실행할 수 없는 SET에는 거절 응답
// `FF ADDR 00 66 00 01 CK`가 온다 - VISCA의 `y0 60 41 FF`에 해당하는 자리다
// (2026-08-12 실측, doc/todo.md 2.4절). 게이트웨이는 아직 이 거절을 합성하지 않는다.
//
// 다만 "대체로"다 - pp가 VISCA 코드와 어긋나는 예외가 지금까지 둘 확인됐고, 원인이
// 서로 다르다:
//
//   1. One Push AF (`18 01`) - pp는 진짜 VISCA 코드가 맞는데 FoMaKo가 그 코드를
//      구현하지 않았다. 카메라 쪽 사정이다. PELCO_EDIS_SET_FOCUS_TRIGGER 참고.
//   2. AWB (`36 00`/`36 05`) - pp 자체가 VISCA 코드가 아니다. VISCA CAM_WB는 0x35인데
//      컨트롤러는 0x36을 보낸다. 컨트롤러 쪽 사정이다. PELCO_EDIS_SET_WB_MODE 참고.
//
// 그래서 새 파라미터를 추가할 때 pp를 VISCA 코드로 가정하면 안 된다 - 실제로 나가는
// 바이트를 봐야 한다.
#define PELCO_EDIS_SET_CMD 0xC3
#define PELCO_EDIS_QUERY_CMD 0xD3
#define PELCO_EDIS_QUERY_RESPONSE 0xD7
// GET 항목 중 유일하게 해독된 것 - Iris/AWB/Focus의 Auto/Manual 상태를 한 번에 묶어
// 돌려준다. 컨트롤러 LED 화면에 표시되는 세 항목이 정확히 이것이다.
#define PELCO_EDIS_QUERY_MODE_STATUS 0x19
// 단일 항목 조회. DATA2가 "무엇을 묻는지"를 VISCA 명령 코드로 지정하는 선택자다.
// 응답은 항목과 무관하게 `FF ADDR 00 D7 00 <code> CK` 형태로 같고, code도 VISCA 값
// 그대로다. 모드 상태 조회(D3 19)와 달리 RESP1에 데이터가 실리지 않고 CMND1(0x00)이
// 그대로 에코되며, 응답만 봐서는 어느 항목의 답인지 알 수 없다(컨트롤러가 질문 순서로
// 짝을 맞춘다).
//
// 처음엔 DATA2가 항상 0x00이라 이 자체를 "전원 조회"로 봤는데, BACK LIGHT 키를 누르면
// `D3 04 33`이 나가고 응답값이 BLC 상태를 따라가는 게 실측으로 확인됐다(2026-08-08).
// 즉 0x04는 항목 선택 조회고 0x00은 그중 CAM_Power였다.
#define PELCO_EDIS_QUERY_ITEM 0x04
#define VISCA_CAM_POWER 0x00       // 0x02 On / 0x03 Standby
#define VISCA_CAM_BACKLIGHT 0x33   // 0x02 On / 0x03 Off
// 응답 RESP1의 상위 니블. 실측한 4대 모두 0x5로 고정이고 컨트롤러가 표시하지 않는
// 항목이라, 정체를 모르는 채로 관측값을 그대로 채운다.
#define PELCO_EDIS_STATUS_RESP1_BASE 0x50
#define PELCO_EDIS_STATUS_IRIS_MANUAL 0x40   // DATA2 bit6
#define PELCO_EDIS_STATUS_FOCUS_MANUAL 0x01  // DATA2 bit0

// One Push AF 키의 SET 파라미터(`C3 18 01`)와, 그걸 카메라로 내보낼 때 실제로 쓰는
// VISCA 코드. 컨트롤러가 보내는 pp(0x18)를 그대로 흘리면 FoMaKo가 Syntax Error
// (`E0 60 02 FF`)로 거절한다 - 매뉴얼 CAM_Focus 표에도 `04 18` 행이 없어서, `04 18`을
// 아예 구현하지 않은 게 실측과 문서 양쪽으로 확인됐다. `04 38 04`로 바꿔 보내야
// 동작한다 (2026-08-08, doc/todo.md 1.1절, handleEdisVendorCommand() 참고).
#define PELCO_EDIS_SET_FOCUS_TRIGGER 0x18   // = VISCA CAM_Focus One Push Trigger (미지원)
#define VISCA_CAM_FOCUS_AF_MODE 0x38        // 02 Auto / 03 Manual / 04 One Push

// AWB 키의 SET 파라미터와, 그걸 카메라로 내보낼 때 쓰는 VISCA 코드.
//
// **pp가 VISCA 코드와 다른 유일한 사례다** (2026-08-12 실측). 컨트롤러가 실제로 보내는
// 바이트는 0x36인데 VISCA CAM_WB는 0x35다:
//
//   FF 03 00 C3 36 00 FC      AWB Auto
//   FF 03 00 C3 36 05 01      AWB Manual
//
// qq(값)는 VISCA 그대로다 - 0x00 Auto, 0x05 Manual이 VISCA CAM_WB 값 표와 정확히 같다.
// 어긋나는 건 명령 코드 한 바이트뿐이라, 0x36 -> 0x35로 바꿔서 내보낸다.
//
// 이전엔 이 파라미터를 0x35로 알고 있었는데, 그건 실측이 아니라 "pp = VISCA 코드"
// 규칙에서 역산한 추정이었다. 실측으로 확인됐던 건 D3 19 **응답**에 실리는 WB 코드가
// VISCA 값이라는 것뿐이고, 요청 쪽 pp는 확인된 적이 없었다. 그래서 실제 컨트롤러의
// AWB 키가 먹지 않았다.
#define PELCO_EDIS_SET_WB_MODE 0x36         // 컨트롤러가 보내는 pp
#define VISCA_CAM_WB_MODE 0x35              // 실제 VISCA 코드 (00 Auto / 05 Manual)

// ---- 상대 조정(Up/Down) 파라미터 ----
// R/B Gain과 BRIGHT 키. AWB와 달리 pp/qq가 표준 VISCA와 정확히 1:1이라 재매핑이 없다
// (실측 2026-08-12):
//
//   FF 03 00 C3 03 02 CB / C3 03 03 CC      R Gain Up / Down    (CAM_RGain)
//   FF 03 00 C3 04 02 CC / C3 04 03 CD      B Gain Up / Down    (CAM_BGain)
//   FF 06 00 C3 0E 02 D9 / C3 0E 03 DA      BRIGHT Up / Down    (CAM_ExpComp)
//
// BRIGHT 키가 CAM_Bright(0x0D)가 아니라 CAM_ExpComp(0x0E)라는 데 주의. 키 라벨만 보고
// 0x0D로 짐작하기 쉬운데 실제로 나가는 바이트는 0x0E였다. 둘은 동작 조건도 다르다 -
// CAM_Bright는 AE 모드가 Bright(`04 39 0D`)일 때만 먹는데, 이 컨트롤러의 IRIS
// AUTO/MANUAL 키는 AE를 Full Auto(0x00)나 Manual(0x03)로만 보내므로 애초에 Bright
// 모드가 될 일이 없다. ExpComp는 자동 노출에 보정값을 얹는 방식이라 Full Auto에서
// 동작하고, 그래서 이 조합에서는 이쪽이 맞는 선택이다.
//
// **지금까지의 파라미터와 성격이 다르다 - 상대 조정이다.** 나머지는 전부 "이 값으로
// 설정하라"는 절대 모드라 modeCache에 담고 조회에 답할 수 있었지만, 이건 "한 칸
// 올려라/내려라"라서 담을 상태가 없다. 그래서 updateModeCacheFromSet()의 default로
// 흘러가 캐시를 건드리지 않고, 확인 조회도 예약하지 않는다 - 의도된 동작이다.
// 절대값을 알려면 VISCA CAM_RGainInq(`09 04 43`)나 CAM_ExpCompPosInq(`09 04 4E`)를
// 따로 던져야 하는데, 컨트롤러가 이 값들을 표시하지도 조회하지도 않으므로 추적할
// 이유가 없다.
//
// 값의 방향이 다른 파라미터와 반대로 보일 수 있으니 주의 - 여기서 0x02는 On/Auto가
// 아니라 Up, 0x03은 Off/Manual이 아니라 Down이다. 세 명령 모두 VISCA 값 표
// (00 Reset / 02 Up / 03 Down)를 그대로 따른다. Reset(0x00)은 컨트롤러에서 관측되지
// 않아 통과 목록에 넣지 않았다.
//
// 전제 조건이 각각 있지만 게이트웨이는 강제하지 않고 그대로 흘려보낸다 - R/B Gain은
// WB가 Manual일 때, ExpComp Up/Down은 ExpComp가 On(`04 3E 02`)일 때 의미가 있다.
// 컨트롤러가 그 순서를 지키는지는 컨트롤러 쪽 문제이고, 실측되지 않은 준비 명령을
// 게이트웨이가 지어내 끼워 넣으면 조작자가 시키지 않은 설정 변경이 된다.
//
// **BRIGHT의 동작 조건이 ED-P와 Sony VISCA에서 반대다 - 아직 미해결이다.**
// 실물 ED-P는 IRIS Manual일 때 밝기가 바뀌고 Auto일 때 거절한다(거절 응답 `0x66`,
// doc/todo.md 2.4절). 그런데 Sony VISCA CAM_ExpComp는 반대로 AE가 Full Auto/우선 모드일
// 때 동작하고 Manual에서는 무효다. 그러니 `04 0E`를 그대로 흘리면 조작자 입장에서
// ED-P에서 되던 조건에서는 안 되고 안 되던 조건에서 되는 상태가 될 수 있다.
//
// 그럼에도 지금은 `04 0E`를 그대로 내보낸다 - 실측된 pp를 근거 없이 바꾸지 않는다는
// 원칙 때문이지, ED-P 쪽 해석(CAM_Iris `04 0B`로 재매핑)이 틀렸다고 판단해서가 아니다.
// FoMaKo에서 IRIS Manual로 두고 BRIGHT를 눌러보면 갈린다 (doc/todo.md 2.5절).
#define VISCA_CAM_RGAIN 0x03                // 00 Reset / 02 Up / 03 Down
#define VISCA_CAM_BGAIN 0x04                // 00 Reset / 02 Up / 03 Down
#define VISCA_CAM_SHUTTER 0x0A              // 00 Reset / 02 Up / 03 Down
#define VISCA_CAM_EXP_COMP 0x0E             // 00 Reset / 02 Up / 03 Down (On/Off는 0x3E)
// Sony VISCA의 노출 관련 상대 조정은 0x0A~0x0E가 연속된 한 벌이다 - Shutter(0A),
// Iris(0B), Gain(0C), Bright(0D), ExpComp(0E). 지금까지 실측된 건 0A와 0E뿐이라 그 둘만
// 통과시킨다. 컨트롤러에 IRIS/GAIN 키가 따로 있다면 0B/0C도 나올 텐데, 실제 바이트를
// 잡기 전에는 넣지 않는다 - AWB(0x36 != VISCA 0x35)에서 "pp = VISCA 코드"를 가정했다가
// 키가 통째로 먹지 않았던 전례가 있다.
#define VISCA_FOCUS_MODE_ONE_PUSH 0x04
#define VISCA_FOCUS_MODE_MANUAL 0x03
// One Push AF를 트리거한 뒤 Focus 모드를 Manual로 되돌리기까지 기다리는 시간.
// 되돌리지 않으면 카메라가 One Push AF 모드에 머물러 수동 초점 조작을 거부한다
// (실측 2026-08-08). 반대로 너무 일찍 되돌리면 초점을 잡는 도중에 끊어버리므로,
// AF 한 번이 끝날 여유를 주는 값이어야 한다.
#define VISCA_ONE_PUSH_AF_SETTLE_MS 2000

// 카메라의 AE/WB/Focus 모드를 VISCA로 조회해 캐시를 갱신하는 주기와, 한 조회의
// 응답을 기다리는 시간. VISCA 조회 응답은 셋 다 `y0 50 pp FF`로 똑같이 생겨서 어느
// 질문의 답인지 구분할 수 없다 - 그래서 한 번에 하나씩만 던지고 답을 받거나
// 타임아웃될 때까지 다음 조회를 보내지 않는다.
#define MODE_INQUIRY_INTERVAL_MS 1000
#define MODE_INQUIRY_TIMEOUT_MS 500
// C3 SET 직후 그 항목만 우선 조회하기까지 기다리는 시간. 라운드로빈을 한 바퀴(항목 4개
// x 1초) 기다리지 않고 바로 확인해서, 카메라가 명령을 거부했을 때 컨트롤러 표시가
// 오래 거짓말하지 않게 한다. 카메라가 명령을 적용할 여유는 줘야 하므로 0은 아니다.
#define MODE_INQUIRY_SET_VERIFY_DELAY_MS 300

// ---- Auto Power Control ----
// 카메라 슬롯별 옵션(off가 기본). on이면 RS485 버스에 흐르는 유효 패킷(체크섬까지
// 통과한, 지정된 카메라 ID와 무관한 모든 패킷)의 양으로 컨트롤러가 활동 중인지
// 판단해 카메라 전원을 자동으로 켜고 끈다 - "컨트롤러가 재잘거리면 켜고, 침묵하면
// 끈다" (main.cpp의 updateAutoPowerFromChatter() 참고). 텀블링(tumbling) 윈도우를
// 쓴다 - 슬라이딩 윈도우만큼 정밀하지 않지만 타임스탬프 배열이 필요 없어 임베디드
// 환경에 더 단순하고, 최대 오차(윈도우 길이 이내)도 이 용도에는 문제가 안 된다.
#define AUTO_POWER_CHATTER_WINDOW_MS 10000
// 윈도우 안에 이 값 이상의 유효 패킷이 있으면 On, 하나도 없으면 Standby. 그 사이
// (1~2개)는 판단을 보류하고 직전 상태를 유지한다 - 어느 쪽으로도 결론 내리기엔
// 근거가 애매한 경계 구간이라, 상태를 자주 뒤집는 것보다 유지하는 쪽이 안전하다.
#define AUTO_POWER_ON_THRESHOLD 3
// CAM_Power On/Standby 명령을 보낸 뒤 실제로 반영됐는지 CAM_PowerInq로 확인하기까지
// 기다리는 시간 - 명령을 보내자마자 상태를 낙관적으로 갱신하지 않고, 카메라가 명령을
// 처리할 시간을 준 뒤 실측으로 확정한다(main.cpp의 reconcileAutoPower() 참고).
#define AUTO_POWER_VERIFY_DELAY_MS 10000
// 확인 결과가 기대와 다르거나(카메라가 명령을 거부) 응답 자체가 없으면(카메라 연결
// 끊김 등) 명령을 다시 보낸다. 재시도 간격은 실패할 때마다 두 배로 늘어나 이 상한에서
// 멈춘다 - 카메라가 응답하지 않는 동안 계속 짧은 간격으로 명령을 퍼붓지 않으면서도,
// WiFi 재연결(kWifiRetryIntervalMs)처럼 포기하지 않고 낮은 빈도로 계속 재시도해 카메라가
// 다시 응답하기 시작하면 자동으로 복구되게 한다.
#define AUTO_POWER_RETRY_MAX_DELAY_MS (5UL * 60UL * 1000UL)

// ---- Pelco-D framing ----
// Pelco-D 프레임은 항상 0xFF로 시작하고 길이가 고정 7바이트다
// (Address, Command1, Command2, Data1, Data2, Checksum).
#define PELCO_D_START_BYTE 0xFF
#define PELCO_D_PACKET_LEN 7
#define PELCO_D_PACKET_TIMEOUT_MS 50

// ---- Pelco-P framing ----
// Pelco-P 프레임은 항상 0xA0으로 시작해 0xAF로 끝나고 길이가 고정 8바이트다
// (Address, Command1, Command2, Data1, Data2, ETX, Checksum). 0xA0/0xAF는
// Pelco-D의 0xFF와 겹치지 않아 같은 버스에서 두 프로토콜을 시작 바이트만으로
// 구분할 수 있다 (Input Protocol의 Pelco-D/P Autodetect 모드가 이를 활용한다).
#define PELCO_P_START_BYTE 0xA0
#define PELCO_P_ETX_BYTE 0xAF
#define PELCO_P_PACKET_LEN 8
#define PELCO_P_PACKET_TIMEOUT_MS 50

// ---- VISCA addressing ----
#define VISCA_ADDR_CAM1 0x81
#define VISCA_ADDR_CAM2 0x82
#define VISCA_ADDR_CAM3 0x83
#define VISCA_ADDR_CAM4 0x84
#define VISCA_ADDR_CAM5 0x85
#define VISCA_ADDR_CAM6 0x86
#define VISCA_ADDR_CAM7 0x87
#define VISCA_ADDR_BROADCAST 0x88
#define VISCA_ADDR_REWRITE_TARGET 0x81

// ---- Network ----
#define DEFAULT_CAMERA_PORT 5678
#define SONY_VISCA_PORT 52381
#define WIFI_CONNECT_TIMEOUT_MS 15000

// ---- Camera routing slots ----
#define CAMERA_SLOT_COUNT 7

// ---- Diagnostics ----
#define DIAG_LOG_DEPTH 20
// Raw byte 로그는 파싱된 RX/TX 로그(DIAG_LOG_DEPTH)와 별도 링버퍼를 쓴다 - 그렇지
// 않으면 raw 트래픽이 잦을 때 유의미한 패킷 로그가 금방 밀려난다.
#define DIAG_RAW_LOG_DEPTH 20
// 라우팅 테이블 카메라 ID(1~7)로 들어왔지만 게이트웨이가 해석하지 못했거나 아직
// 구현하지 않은 명령의 최근 기록. Debug Mode를 안 켜놔도 나중에 와서 확인할 수 있게
// 하는 게 목적이라 위 두 로그보다 깊이가 얕다 - 완전히 같은 카메라+원본 바이트가
// 반복되면 새 항목을 추가하지 않고 발생 횟수만 올리므로(Diagnostics::recordUnhandledPacket
// 참고), 10개면 서로 다른 미해석 명령 10가지를 담기에 충분하다.
#define DIAG_UNHANDLED_LOG_DEPTH 10

// ---- Raw Byte Monitor ----
// Live Packet Monitor와 달리 프로토콜 파싱/체크섬 결과와 무관하게 RS485로 들어오는
// 모든 바이트를 그대로 hex로 보여준다. 이 시간(ms) 이상 새 바이트가 없으면 한 줄을
// 끊어서 다음 버스트를 새 줄에 출력한다 (프로토콜 프레이밍 타임아웃과 무관한, 순수
// 가독성용 구분자).
#define RAW_MONITOR_GAP_MS 50

// Diagnostics의 raw 로그 한 줄이 가질 수 있는 최대 글자 수. 이걸 넘으면 유휴 간격을
// 기다리지 않고 그 자리에서 줄을 끊는다.
//
// **상한이 없으면 힙을 다 먹는다.** 그 버퍼(main.cpp의 diagRawLineBuf)는 위
// RAW_MONITOR_GAP_MS 이상 버스가 조용해져야만 비워지는데, 컨트롤러 폴링 간격이 그보다
// 짧으면 줄이 영영 안 끊긴다 - 그리고 실제 간격 분포는 아직 측정되지 않았다
// (doc/todo.md 1.5절). 9600bps 연속 트래픽이면 초당 2.8KB씩 자라고, Arduino String은
// 16바이트마다 realloc하므로 누적 복사량이 길이의 제곱으로 늘어난다. 100KB에 닿기
// 훨씬 전에 loop()가 먼저 느려져 RS485 프레임을 잃는다.
//
// 512자면 raw 바이트 약 170개로, Pelco-D 프레임 24개에 해당한다 - 한 화면에 보기에도
// 이 정도가 상한이다.
#define DIAG_RAW_LINE_MAX_CHARS 512

// ---- Web PTZ Controller (doc/Web_controller.md) ----
// 게이트웨이가 직접 명령의 출발점이 되는 기능이다 - 브라우저에서 누른 것을 IP 카메라에는
// VISCA로, RS485 카메라에는 Pelco-D 마스터 프레임으로 내보낸다.

// RS485로 프레임을 내보내기 전에 기다리는 버스 유휴 시간. 이 버스에는 ZU-EPC7000이
// 이미 마스터로 있고 쉬지 않고 폴링하므로, 아무 때나 송신하면 드라이버 두 개가 동시에
// 버스를 문다. 게다가 게이트웨이는 자기 송신 중에 버스를 들을 수 없어(DE/RE가 수신부를
// 끈다) 충돌을 감지할 방법도 없다 - 그래서 "마지막 수신 바이트로부터 조용한 시간"을
// 유일한 판단 근거로 삼는다.
//
// 9600bps에서 Pelco-D 한 프레임(7바이트)은 약 7.3ms, 프레임 내부의 바이트 간격은 1ms
// 남짓이다. 30ms면 프레임 중간을 유휴로 오인하지 않으면서도 폴링 사이에 들어갈 만큼 짧다.
#define WEB_TX_BUS_IDLE_MS 30
// 유휴 창을 이 시간 안에 못 잡으면 프레임을 버린다. 무한정 쌓아두면 조작자가 손을 뗀
// 한참 뒤에 명령이 뒤늦게 나가는 게 더 위험하다.
#define WEB_TX_MAX_WAIT_MS 500
#define WEB_TX_QUEUE_DEPTH 8

// 이동/줌/포커스 명령의 유효 기간. 브라우저는 물리 조이스틱과 달리 "손을 놓으면
// 중심으로 돌아온다"는 보장이 없다 - Wi-Fi가 끊기거나 탭이 죽으면 마지막 이동 명령이
// 그대로 유지된다(VISCA Pan-tiltDrive는 Stop이 올 때까지 도는 래치 명령이다).
// 그래서 움직임을 "명시적으로 갱신되는 동안만 유지되는 임대"로 다룬다 - 브라우저가
// 이 시간 안에 같은 명령을 다시 보내지 않으면 게이트웨이가 스스로 Stop을 만든다.
// 브라우저 쪽 갱신 주기(300ms)의 두 배 이상이어야 정상 조작 중에 끊기지 않는다.
#define WEB_HOLD_TIMEOUT_MS 700

// 이 시간 안에 유효한 RS485 프레임이 있었으면 "물리 컨트롤러가 활동 중"으로 표시한다.
// 웹 조작자에게 다른 사람이 같은 카메라를 만지고 있을 수 있다는 걸 알리는 용도다.
#define WEB_BUS_ACTIVE_MS 3000

// 버스에서 엿들은 D3 조회를 D7 응답과 짝지을 수 있는 시간. D3 04 응답은 어느 항목의
// 답인지를 바이트에 담지 않아서 "무엇을 물었는지"를 기억해야만 해석된다 - 너무 오래
// 붙들고 있으면 엉뚱한 응답과 짝지어 잘못된 상태를 표시하게 된다.
#define WEB_BUS_QUERY_PAIR_MS 500

// ---- Web Config Server (AP+STA) ----
// USB Serial에 물리적으로 접근할 수 없는 환경(이미 설치된 장비, UART0가 RS485와
// 충돌하는 보드 등)을 위한 두 번째 설정 인터페이스. STA(평소 WiFi)와 무관하게
// AP를 항상 띄워서 접근 경로를 보장한다. SSID는 실행 중에 AP_SSID_PREFIX +
// MAC 주소 뒷자리로 조립되어 기기별로 겹치지 않는다 (WebConfigServer::begin() 참고).
// 비밀번호는 고정 기본값 - "웹 페이지 자체엔 로그인이 없다"는 것과는 별개로,
// AP WiFi 접속 자체를 아무나 못 잡게 막는 최소한의 방어선이다.
#define AP_SSID_PREFIX "RS485Gateway-"
#define AP_PASSWORD_DEFAULT "00000001"
#define WEB_SERVER_PORT 80
