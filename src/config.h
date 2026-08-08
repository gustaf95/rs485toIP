#pragma once

#include <Arduino.h>

// ---- RS485 / UART2 ----
#define RS485_RX_PIN_DEFAULT 25
#define RS485_TX_PIN_DEFAULT 26
#define RS485_DE_RE_PIN_DEFAULT 27
#define RS485_BAUD_DEFAULT 9600

// UART 신호 반전 여부의 초기 기본값. A/B(D+/D-)가 뒤집혀 결선된 배선에서는 이걸 켜야
// 수신 바이트가 안 깨진다(0xFF 프레임이 0x00으로 읽히고 나머지는 한 비트씩 밀린 보수값이
// 된다) - 그런 배선이면 RS485 Settings 화면에서 켜야 한다 (RoutingTable::applyDefaults()
// 참고). 정상 결선(A/B 안 뒤집힘)이 기본 가정이라 false로 둔다.
#define RS485_INVERT_DEFAULT false

// USB 콘솔(UART0)의 속도. RS485 속도와 무관하며, Debug Mode 로그가 loop()를 오래
// 붙잡지 않도록 최대한 빠르게 잡는다 (main.cpp setup()의 주석 참고). 이 값을 바꾸면
// platformio.ini의 monitor_speed도 같이 맞춰야 한다.
// UART0 Shared Mode에서는 이 값을 쓰지 않는다 - 그 모드에서는 Serial이 곧 RS485
// 데이터 라인이라 rs485Baudrate로 열린다.
#define SERIAL_CONSOLE_BAUD 115200

// RS485 UART 수신 링버퍼 크기. 기본값(256)이면 Debug Mode 로그처럼 loop()를 잠시
// 붙잡는 작업 중에 들어온 바이트가 넘쳐 유실된다. 9600bps 기준 1024바이트면 약 1초의
// 정체를 견딘다.
#define RS485_RX_BUFFER_SIZE 1024

// ---- RS485 / UART0 Shared Mode ----
// 일부 보드 리비전은 RS485 트랜시버가 UART0(RX0/TX0)에 물리적으로 고정 결선되어
// 있어 USB 콘솔과 Serial을 공유한다. 이 모드에서는 RX/TX/DE-RE 핀이 모두 이 값으로
// 고정되며 사용자가 바꿀 수 없다 (GatewayActions::setRs485Uart0SharedMode() 참고).
#define RS485_UART0_SHARED_RX_PIN 3   // RX0
#define RS485_UART0_SHARED_TX_PIN 1   // TX0
#define RS485_UART0_SHARED_DE_RE_PIN 17

// ---- Status LED ----
// RS485 Settings 화면(Serial/Web)에서 런타임에 바꿀 수 있다 - 이 값은 초기 기본값일
// 뿐이다 (RoutingTable::applyDefaults() 참고).
#define STATUS_LED_PIN_DEFAULT 13

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
#define VISCA_DUPLICATE_SUPPRESS_MS 200

// ---- EDIS ED-P 벤더 확장 (Pelco-D) ----
// ZU-EPC7000 <-> EDIS ED-P 사이에서 실측한 벤더 고유 확장 명령이다. 표준 Pelco-D
// 확장 옵코드 표(0x03~0x6F) 밖의 값이라 표준 문서에는 없다. 둘 다 CMND1=0x00을 쓴다.
//
//   SET:   FF ADDR 00 C3 pp qq CK   -> VISCA `8x 01 04 pp qq FF` 와 1:1 대응
//   GET:   FF ADDR 00 D3 pp ?? CK   -> pp가 조회할 항목
//   응답:  FF ADDR R1 D7 pp val CK
//
// SET의 pp/qq가 VISCA 명령 코드/값과 그대로 같다는 게 실측으로 확인됐다 (Iris
// Auto/Manual = `39 00`/`39 03`, Focus Auto/Manual = `38 02`/`38 03`, One Push AF =
// `18 01`). SET에는 응답이 없고, 컨트롤러는 주기적인 GET 폴링으로 화면을 갱신한다.
// 다만 pp가 곧 VISCA 코드라는 게 "FoMaKo가 그 VISCA 코드를 지원한다"는 뜻은 아니다 -
// One Push AF(`18 01`)가 실제로 그랬고, 유일하게 1:1이 아닌 예외가 됐다. 아래
// PELCO_EDIS_SET_FOCUS_TRIGGER 참고.
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

// ---- Raw Bridge ----
// RAW_BRIDGE Input Protocol에서 RS485로 들어오는 바이트를 UDP로 묶어 보낼 때 쓰는
// 값. 한 번에 한 바이트씩 UDP로 쏘면 오버헤드가 크므로, 이 시간(ms) 이상 새 바이트가
// 없을 때(=한 명령의 바이트가 다 도착했다고 볼 수 있을 때) 모아뒀던 바이트를 한
// UDP 패킷으로 전송한다. Raw Byte Monitor의 구분 기준(RAW_MONITOR_GAP_MS)과 값은
// 같지만, 이쪽은 실제 네트워크 전송 타이밍에 영향을 주는 별개의 설정이라 분리했다.
#define RAW_BRIDGE_GAP_MS 20
// 버퍼가 이 크기에 도달하면 gap을 기다리지 않고 즉시 전송한다 (VISCA_BUFFER_SIZE와
// 동일하게 맞춰 이 프로젝트의 다른 버퍼들과 일관성을 유지한다).
#define RAW_BRIDGE_BUFFER_SIZE VISCA_BUFFER_SIZE

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
