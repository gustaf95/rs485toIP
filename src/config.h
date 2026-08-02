#pragma once

#include <Arduino.h>

// ---- RS485 / UART2 ----
#define RS485_RX_PIN_DEFAULT 25
#define RS485_TX_PIN_DEFAULT 26
#define RS485_DE_RE_PIN_DEFAULT 27
#define RS485_BAUD_DEFAULT 9600

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
