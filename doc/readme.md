# ESP32 RS485 VISCA to IP VISCA Gateway

## 1. 프로젝트 목적

본 프로젝트는 기존 RS485 방식 PTZ 컨트롤러로 FoMaKo PTZ IP Camera를 제어하기 위한 ESP32 기반 변환 게이트웨이를 구현한다.

현재 사용 중인 PTZ 컨트롤러는 RS485를 통해 VISCA 명령을 출력한다. 반면 추가하려는 FoMaKo PTZ 카메라는 IP 기반 제어를 지원한다. 따라서 ESP32는 RS485로 들어오는 VISCA 명령을 수신하고, 이를 IP VISCA 또는 Sony VISCA over IP 명령으로 변환하여 네트워크 카메라로 전달한다.

최종 목표는 다음과 같다.

```text
기존 PTZ Controller
  → RS485 VISCA
  → ESP32 Gateway
  → IP VISCA / Sony VISCA over IP
  → FoMaKo PTZ IP Camera
```

이 게이트웨이는 기존 1~4번 카메라는 그대로 유지하고, 새로 추가할 **5번 카메라**를 IP PTZ 카메라로 연결하기 위해 사용한다.

---

## 2. 대상 카메라 및 프로토콜 정보

FoMaKo 매뉴얼 기준 주요 제어 정보는 다음과 같다.

| 구분 | 지원 내용 |
|---|---|
| 시리얼 제어 인터페이스 | RS232, RS485, RS422 |
| 시리얼 제어 프로토콜 | VISCA, Pelco-D, Pelco-P |
| 네트워크 제어 프로토콜 | VISCA OVER IP, IP VISCA, ONVIF |
| IP VISCA 포트 | 5678 |
| Sony VISCA 포트 | 52381 |
| ONVIF 포트 | 2000 |
| 지원 Baudrate | 2400, 4800, 9600, 38400, 115200 bps |
| 시리얼 형식 | 8 data bits, 1 stop bit, no parity |

본 프로젝트에서는 다음 우선순위로 제어 방식을 사용한다.

| 우선순위 | 방식 | 포트 | 설명 |
|---:|---|---:|---|
| 1 | IP VISCA | 5678 | 기본 제어 방식. Raw VISCA payload를 UDP로 전송 |
| 2 | Sony VISCA over IP | 52381 | IP VISCA 5678이 동작하지 않을 경우 fallback |
| 제외 | ONVIF | 2000 | 초기 구현 범위에서 제외 |

---

## 3. 최종 시스템 구성

```text
┌──────────────────────────────┐
│ Existing PTZ Controller       │
│ - RS485 VISCA output          │
│ - Camera 1~4 already in use   │
│ - Camera 5 to be added        │
└──────────────┬───────────────┘
               │
               │ RS485 / VISCA
               │
┌──────────────▼───────────────┐
│ 3.3V RS485 Transceiver        │
│ MAX3485 / SP3485 / ADM3485    │
└──────────────┬───────────────┘
               │ UART2
               │
┌──────────────▼───────────────┐
│ ESP32 Gateway                 │
│ - RS485 VISCA Receiver        │
│ - VISCA Packet Parser         │
│ - Camera ID Router            │
│ - IP VISCA Client             │
│ - Sony VISCA Fallback Client  │
│ - Web Configuration UI        │
│ - Flash/NVS Configuration     │
│ - Debug and Status Monitor    │
└──────────────┬───────────────┘
               │ Wi-Fi or Ethernet
               │
┌──────────────▼───────────────┐
│ FoMaKo PTZ IP Camera          │
│ - Assigned as Camera 5        │
│ - IP VISCA port 5678          │
│ - Sony VISCA port 52381       │
└──────────────────────────────┘
```

ESP32는 RS485 버스에서 들어오는 모든 VISCA 패킷을 수신하되, 기본적으로 **Camera 5에 해당하는 VISCA 주소만 IP 카메라로 라우팅**한다.

---

## 4. 기본 카메라 매핑 정책

기존 시스템에는 1~4번 카메라가 이미 존재한다. 이 게이트웨이는 우선 **5번 카메라만 추가 제어**하는 용도로 사용한다.

VISCA 주소 바이트는 일반적으로 다음과 같이 사용한다.

| 카메라 번호 | VISCA Address Byte | 처리 방식 |
|---:|---|---|
| 1 | 0x81 | 기존 장비에서 처리. ESP32는 기본적으로 무시 |
| 2 | 0x82 | 기존 장비에서 처리. ESP32는 기본적으로 무시 |
| 3 | 0x83 | 기존 장비에서 처리. ESP32는 기본적으로 무시 |
| 4 | 0x84 | 기존 장비에서 처리. ESP32는 기본적으로 무시 |
| 5 | 0x85 | ESP32가 IP 카메라로 전달 |
| Broadcast | 0x88 | 설정에 따라 무시 또는 등록된 IP 카메라에 전달 |

초기 기본 Target Device는 다음과 같이 설정한다.

| 항목 | 기본값 |
|---|---|
| Device Name | CAM5 |
| RS485 VISCA ID | 5 |
| VISCA Address Byte | 0x85 |
| Camera IP | 웹 설정에서 입력 |
| Camera Port | 5678 |
| Protocol Mode | IP VISCA |
| Enabled | true |

펌웨어는 추후 확장을 위해 다중 Target Device 구조를 가져야 하지만, 기본 설정은 CAM5 하나만 활성화한다.

---

## 5. 하드웨어 요구사항

## 5.1 ESP32 UART 사용

| 용도 | ESP32 인터페이스 | 권장 핀 | 비고 |
|---|---|---|---|
| PC 연결 / 디버깅 / 펌웨어 업로드 | UART0 / Serial | TX0, RX0 | USB Serial 전용 |
| RS485 VISCA 수신/송신 | UART2 / Serial2 | RX2 = GPIO16, TX2 = GPIO17 | RS485 모듈 연결 |
| RS485 방향 제어 | GPIO | GPIO4 | DE, /RE 제어 |
| 설정 버튼 | GPIO | 추후 지정 | AP 모드 진입 또는 설정 초기화 |
| 상태 LED | GPIO | 추후 지정 | Wi-Fi, RS485 RX, IP TX 상태 표시 |

중요 사항:

- UART0는 USB Serial 디버그와 업로드 전용으로 사용한다.
- RS485는 반드시 UART2에 연결한다.
- RS485를 TX0/RX0에 연결하지 않는다.
- TX0/RX0를 RS485에 연결하면 USB 시리얼 디버그, 업로드, RS485 제어가 충돌할 수 있다.

## 5.2 RS485 트랜시버

ESP32는 3.3V TTL 로직을 사용하므로 RS485 모듈도 3.3V TTL 호환 제품을 사용한다.

권장 칩은 다음과 같다.

| 칩 | 설명 |
|---|---|
| MAX3485 | ESP32에 적합한 3.3V RS485 트랜시버 |
| SP3485 | ESP32에 적합한 3.3V RS485 트랜시버 |
| ADM3485 | 3.3V RS485 트랜시버 |
| MAX13487E | 자동 방향 제어 가능 RS485 트랜시버 |

일반 MAX485 모듈은 대부분 5V Arduino용이므로 ESP32 RX 핀에 5V 신호가 들어가지 않도록 주의한다.

## 5.3 RS485 배선

| RS485 모듈 핀 | ESP32 핀 |
|---|---|
| VCC | 3V3 |
| GND | GND |
| RO | GPIO16 / RX2 |
| DI | GPIO17 / TX2 |
| DE | GPIO4 |
| /RE | GPIO4 |
| A 또는 + | PTZ 컨트롤러 RS485 A / + |
| B 또는 - | PTZ 컨트롤러 RS485 B / - |

DE와 /RE를 하나의 GPIO에 묶어 사용하는 경우 다음처럼 동작한다.

| GPIO 상태 | 동작 |
|---|---|
| LOW | 수신 모드 |
| HIGH | 송신 모드 |

기본 상태는 수신 모드이다.

```text
DE = LOW
/RE = LOW
```

---

## 6. 펌웨어 기능 요구사항

## 6.1 RS485 VISCA 수신

펌웨어는 RS485 UART에서 들어오는 VISCA 패킷을 안정적으로 수신해야 한다.

요구사항:

1. UART2를 설정된 Baudrate로 초기화한다.
2. RS485에서 들어오는 바이트를 지속적으로 읽는다.
3. 수신 바이트를 VISCA 버퍼에 저장한다.
4. `0xFF`를 수신하면 하나의 VISCA 패킷으로 판단한다.
5. 최소 패킷 길이를 검사한다.
6. 패킷 첫 바이트를 확인하여 대상 카메라 ID를 판별한다.
7. CAM5에 해당하는 `0x85` 패킷은 IP 카메라로 전송한다.
8. CAM1~CAM4에 해당하는 `0x81~0x84` 패킷은 기본적으로 무시한다.
9. 브로드캐스트 `0x88`은 설정값에 따라 무시하거나 등록된 대상 카메라에 전달한다.
10. 버퍼 오버플로우 발생 시 버퍼를 초기화하고 오류 카운터를 증가시킨다.
11. 패킷 timeout 발생 시 불완전한 패킷을 폐기한다.

권장값:

| 항목 | 값 |
|---|---|
| VISCA buffer size | 128 bytes |
| Packet timeout | 50 ms |
| 기본 Baudrate | 9600 bps |
| 시리얼 형식 | 8N1 |

## 6.2 VISCA 주소 라우팅

ESP32는 VISCA 패킷의 첫 바이트를 확인하여 Target Device를 선택한다.

기본 라우팅 테이블:

| VISCA Address Byte | 의미 | 기본 처리 |
|---|---|---|
| 0x81 | CAM1 | 무시 |
| 0x82 | CAM2 | 무시 |
| 0x83 | CAM3 | 무시 |
| 0x84 | CAM4 | 무시 |
| 0x85 | CAM5 | FoMaKo IP 카메라로 전송 |
| 0x88 | Broadcast | 설정에 따라 처리 |

주소 라우팅은 웹 설정 화면에서 수정 가능해야 한다.

## 6.3 IP VISCA 전송

기본 전송 방식은 UDP 기반 Raw IP VISCA이다.

요구사항:

1. 등록된 Target Device의 IP와 포트를 읽는다.
2. 수신한 VISCA 패킷을 UDP payload로 그대로 전송한다.
3. 기본 포트는 5678이다.
4. 전송 성공/실패 여부를 카운터에 기록한다.
5. 전송 실패가 발생해도 펌웨어는 멈추지 않는다.

예시:

```text
RS485 input:
85 01 06 04 FF

UDP payload:
85 01 06 04 FF

Target:
CAM5_IP:5678
```

주의: 일부 카메라는 IP VISCA에서 주소 바이트 `0x81`만 기대할 수 있다. 따라서 Target Device별로 다음 옵션을 제공한다.

| 옵션 | 설명 |
|---|---|
| Preserve VISCA address | RS485에서 받은 첫 바이트를 그대로 전송 |
| Rewrite to 0x81 | IP 카메라로 보낼 때 첫 바이트를 0x81로 변경 |

CAM5 기본값은 `Rewrite to 0x81`을 권장한다. 즉, 컨트롤러에서 `0x85`로 들어온 명령을 IP 카메라에는 단일 카메라 주소인 `0x81`로 바꾸어 보낼 수 있어야 한다.

예시:

```text
RS485 input from controller:
85 01 06 04 FF

UDP payload to single IP camera:
81 01 06 04 FF
```

## 6.4 Sony VISCA fallback

Target Device별로 Protocol Mode를 선택할 수 있어야 한다.

| Protocol Mode | 설명 |
|---|---|
| IP_VISCA_RAW_UDP | Raw VISCA payload를 UDP 5678로 전송 |
| SONY_VISCA_IP | Sony VISCA over IP framing 후 52381로 전송 |

기본값은 `IP_VISCA_RAW_UDP`이다.

Sony VISCA mode는 다음 항목을 처리할 수 있도록 구조를 분리한다.

- Sony VISCA over IP header
- payload length
- sequence number
- VISCA payload
- response packet parsing

## 6.5 응답 처리

컨트롤러 호환성을 위해 응답 처리 모드를 제공한다.

FoMaKo 매뉴얼 기준 VISCA 응답 예시는 다음과 같다.

| 응답 | 패킷 | 의미 |
|---|---|---|
| ACK | `z0 41 FF` | 명령 수락 |
| Completion | `z0 51 FF` | 명령 실행 완료 |

`z`는 camera address + 8이다.

응답 모드는 다음 중 선택 가능해야 한다.

| Response Mode | 설명 |
|---|---|
| none | RS485 컨트롤러로 응답을 보내지 않음 |
| synthetic | ESP32가 ACK/Completion을 생성하여 RS485로 반환 |
| forward | IP 카메라의 응답을 받아 RS485로 전달 |

CAM5 기본값은 `synthetic`을 권장한다. 컨트롤러가 응답을 요구하지 않는 경우 `none`으로 설정할 수 있다.

## 6.6 Stop 명령 우선 처리

PTZ 제어에서 가장 중요한 것은 Stop 명령이 누락되지 않는 것이다. 펌웨어는 Pan/Tilt/Zoom Stop 명령을 가능한 한 즉시 전송해야 한다.

요구사항:

- Stop 명령은 전송 큐에서 우선 처리한다.
- Wi-Fi 재접속 중에는 Stop 명령 누락 가능성을 로그로 남긴다.
- 패킷 처리 지연으로 카메라가 계속 움직이는 상황을 최소화한다.

대표 Stop 명령 예시는 다음과 같다.

| 기능 | VISCA 명령 예시 |
|---|---|
| Pan/Tilt Stop | `81 01 06 01 00 00 03 03 FF` |
| Zoom Stop | `81 01 04 07 00 FF` |

---

## 7. 설정 저장 요구사항

모든 설정값은 ESP32 Flash에 저장되어야 하며, 재부팅 후 자동으로 복원되어야 한다.

ESP32 Arduino 환경에서는 `Preferences` 또는 NVS를 사용한다.

저장해야 할 설정값:

| 설정 항목 | 설명 | 기본값 |
|---|---|---|
| Wi-Fi SSID | 접속할 무선 AP 이름 | 없음 |
| Wi-Fi Password | 무선 AP 비밀번호 | 없음 |
| ESP32 IP Mode | DHCP 또는 Static | DHCP |
| ESP32 Static IP | 고정 IP 사용 시 ESP32 IP | 없음 |
| Gateway | 고정 IP Gateway | 없음 |
| Subnet Mask | 고정 IP Subnet | 없음 |
| RS485 Baudrate | PTZ 컨트롤러 Baudrate | 9600 |
| RS485 RX Pin | UART2 RX 핀 | GPIO16 |
| RS485 TX Pin | UART2 TX 핀 | GPIO17 |
| RS485 DE/RE Pin | 방향 제어 핀 | GPIO4 |
| Broadcast Handling | 0x88 처리 방식 | ignore |
| Target Device List | PTZ 카메라 목록 | CAM5 1개 |
| Debug Log Level | 로그 수준 | info |

---

## 8. Target Device 관리 요구사항

웹 설정 화면에서 Target Device, 즉 PTZ 카메라를 추가, 수정, 삭제할 수 있어야 한다.

각 Target Device는 다음 정보를 가진다.

| 항목 | 설명 | CAM5 기본값 |
|---|---|---|
| Device Name | 카메라 이름 | CAM5 |
| Enabled | 사용 여부 | true |
| RS485 VISCA ID | PTZ 컨트롤러에서 사용하는 카메라 번호 | 5 |
| Input VISCA Address Byte | RS485에서 들어오는 주소 바이트 | 0x85 |
| Output VISCA Address Mode | 주소 유지 또는 0x81로 변경 | rewrite_to_0x81 |
| Camera IP | 대상 PTZ 카메라 IP | 사용자가 입력 |
| Camera Port | IP VISCA 또는 Sony VISCA 포트 | 5678 |
| Protocol Mode | IP VISCA / Sony VISCA | IP_VISCA_RAW_UDP |
| Response Mode | none / synthetic / forward | synthetic |
| Notes | 메모 | FoMaKo PTZ IP Camera |

기본 등록 장치:

| Device Name | RS485 VISCA ID | Input Address | Output Address Mode | Camera IP | Port | Protocol | Enabled |
|---|---:|---|---|---|---:|---|---|
| CAM5 | 5 | 0x85 | rewrite_to_0x81 | 설정 필요 | 5678 | IP_VISCA_RAW_UDP | true |

---

## 9. 웹 설정 및 모니터링 화면

ESP32는 웹 기반 설정 및 모니터링 화면을 제공해야 한다.

## 9.1 Dashboard

Dashboard에는 다음 정보를 표시한다.

| 항목 | 설명 |
|---|---|
| ESP32 IP | 현재 ESP32의 IP 주소 |
| Wi-Fi 연결 상태 | 연결됨 / 연결 안 됨 |
| 현재 SSID | 접속 중인 AP 이름 |
| RS485 Baudrate | 현재 RS485 통신 속도 |
| 등록된 Target Device 수 | 카메라 목록 개수 |
| CAM5 상태 | enabled, IP, port, protocol 표시 |
| 마지막 RS485 수신 패킷 | HEX 문자열 |
| 마지막 IP 전송 패킷 | HEX 문자열 |
| 마지막 라우팅 결과 | ignored / sent to CAM5 / broadcast |
| RS485 RX Count | 수신 패킷 수 |
| IP TX Count | 전송 패킷 수 |
| Error Count | 오류 카운터 |
| Uptime | ESP32 동작 시간 |

## 9.2 Wi-Fi 설정 화면

다음 값을 설정할 수 있어야 한다.

| 항목 | 설명 |
|---|---|
| SSID | 무선 AP 이름 |
| Password | 무선 AP 비밀번호 |
| DHCP 사용 여부 | DHCP 또는 Static IP 선택 |
| Static IP | 고정 IP 사용 시 ESP32 IP |
| Gateway | 고정 IP Gateway |
| Subnet Mask | 고정 IP Subnet |

Wi-Fi 접속 실패 시 ESP32는 fallback AP 모드로 진입한다.

Fallback AP 기본값:

| 항목 | 값 |
|---|---|
| AP SSID | ESP32-VISCA-GW |
| AP Password | visca1234 |
| AP IP | 192.168.4.1 |

## 9.3 Target Device 설정 화면

다음 기능을 제공한다.

- Target Device 추가
- Target Device 수정
- Target Device 삭제
- Device 활성화/비활성화
- RS485 VISCA ID 설정
- Input VISCA Address Byte 설정
- Output VISCA Address Mode 설정
- Camera IP 설정
- Camera Port 설정
- Protocol Mode 설정
- Response Mode 설정

우선 CAM5를 기본 등록하고, 사용자는 웹 화면에서 CAM5의 IP 주소만 입력해도 동작할 수 있어야 한다.

## 9.4 진단 화면

다음 정보를 표시한다.

| 항목 | 설명 |
|---|---|
| 최근 RS485 수신 로그 | 최근 수신된 VISCA 패킷 목록 |
| 최근 IP 전송 로그 | 최근 전송된 UDP 패킷 목록 |
| 최근 무시된 패킷 로그 | CAM1~CAM4 등 무시된 패킷 목록 |
| 잘못된 패킷 수 | malformed packet count |
| 버퍼 오버플로우 수 | buffer overflow count |
| Packet timeout 수 | incomplete packet timeout count |
| Wi-Fi 재접속 횟수 | reconnect count |
| Uptime | ESP32 동작 시간 |

---

## 10. 안정성 요구사항

실사용 가능한 최종 형태를 기준으로 다음 안정성 기능을 포함한다.

| 항목 | 요구사항 |
|---|---|
| Wi-Fi 재접속 | 연결이 끊어지면 자동 재접속 |
| Watchdog | 펌웨어 멈춤 방지 |
| Packet timeout | 불완전한 VISCA 패킷 자동 폐기 |
| Buffer overflow 처리 | 버퍼 초기화 및 오류 카운터 증가 |
| Stop 명령 우선 처리 | PTZ 정지 명령 누락 방지 |
| 설정값 복구 | 재부팅 후 Flash 저장값 로드 |
| AP fallback | Wi-Fi 접속 실패 시 설정용 AP 모드 진입 |
| Safe default | 설정이 없으면 CAM5 기본 구조로 시작 |
| Logging | 최근 패킷과 오류 상태 확인 가능 |

---

## 11. 구현 제외 항목

최종형 요구사항에서도 다음 기능은 구현하지 않는다.

| 제외 항목 | 이유 |
|---|---|
| ONVIF 제어 | PTZ 제어 브리지 목적에 비해 구현 복잡도가 높음 |
| 영상 스트리밍 | 본 장치는 제어 신호 변환 장치임 |
| RTSP 처리 | 제어 기능과 무관 |
| Pelco-D/P 변환 | 현재 목표는 VISCA 변환 |
| NVR/VMS 연동 | 본 프로젝트 범위 밖 |
| 인터넷 외부 접속 기능 | 내부망 사용 전제 |

---

## 12. 테스트 기준

프로젝트 성공 기준은 다음과 같다.

| 테스트 | 기대 결과 |
|---|---|
| ESP32 Wi-Fi 연결 | ESP32가 IP를 할당받음 |
| 웹 화면 접속 | 브라우저에서 ESP32 설정 화면 접근 가능 |
| CAM5 설정 | 웹 화면에서 CAM5 IP와 포트 설정 가능 |
| 설정 저장 | 재부팅 후 CAM5 설정값 유지 |
| RS485 수신 | PTZ 컨트롤러 조작 시 VISCA HEX 패킷 수신 |
| CAM1~CAM4 패킷 처리 | 기본적으로 IP 전송하지 않고 무시 |
| CAM5 패킷 처리 | `0x85` 패킷을 CAM5 IP로 전송 |
| 주소 변환 | 설정에 따라 `0x85`를 `0x81`로 변환하여 전송 |
| IP VISCA 전송 | 수신한 명령이 CAM5 IP:5678로 전송됨 |
| Pan/Tilt 조작 | 컨트롤러에서 5번 카메라 선택 후 카메라가 움직임 |
| Stop 조작 | 조이스틱을 놓으면 카메라가 멈춤 |
| Zoom 조작 | Zoom In/Out 동작 |
| Preset 호출 | 저장된 preset으로 이동 |
| USB Serial 디버그 | RS485와 독립적으로 로그 확인 가능 |
| Wi-Fi 재접속 | Wi-Fi 재연결 후 제어 기능 복구 |

---

## 13. 권장 소스 구조

```text
/src
  main.cpp
  config.h
  ViscaParser.h
  ViscaParser.cpp
  Rs485Port.h
  Rs485Port.cpp
  IpViscaClient.h
  IpViscaClient.cpp
  SonyViscaClient.h
  SonyViscaClient.cpp
  DeviceConfig.h
  DeviceConfig.cpp
  DeviceRouter.h
  DeviceRouter.cpp
  WebConfigServer.h
  WebConfigServer.cpp
  Diagnostics.h
  Diagnostics.cpp
```

각 모듈의 역할은 다음과 같다.

| 모듈 | 역할 |
|---|---|
| ViscaParser | `0xFF` 기준 VISCA 패킷 파싱 |
| Rs485Port | UART2 및 DE/RE 제어 |
| IpViscaClient | Raw UDP IP VISCA 전송 |
| SonyViscaClient | Sony VISCA over IP framing 및 전송 |
| DeviceConfig | Target Device 설정 구조체 및 저장/로드 |
| DeviceRouter | VISCA 주소 기반 라우팅 및 주소 변환 |
| WebConfigServer | 웹 설정 UI 제공 |
| Diagnostics | 로그, 카운터, 상태 정보 관리 |

---

## 14. Codex 구현 지시사항

이 프로젝트는 Arduino ESP32 펌웨어로 구현한다.

단계별 proof-of-concept가 아니라, 위 요구사항을 기준으로 최종형 구조를 작성한다. 단, 코드 구현 시 기능은 모듈별로 분리하고, 기본 동작은 CAM5 하나를 대상으로 한다.

구현 핵심 요구사항:

1. UART0는 USB Serial 디버그 전용으로 사용한다.
2. UART2를 사용하여 RS485 VISCA 패킷을 수신한다.
3. VISCA 패킷은 `0xFF`를 기준으로 구분한다.
4. 패킷 첫 바이트로 VISCA 주소를 판별한다.
5. 기본적으로 `0x85` 주소를 CAM5로 라우팅한다.
6. CAM1~CAM4 주소인 `0x81~0x84`는 기본적으로 무시한다.
7. CAM5 Target Device는 기본 등록되어 있어야 한다.
8. CAM5의 IP 주소, 포트, 프로토콜 모드, 응답 모드는 웹 화면에서 설정 가능해야 한다.
9. CAM5 기본 포트는 5678이다.
10. 기본 Protocol Mode는 Raw UDP IP VISCA이다.
11. CAM5 전송 시 VISCA 주소를 `0x85` 그대로 보낼지 `0x81`로 바꿀지 설정 가능해야 한다.
12. 기본 Output VISCA Address Mode는 `rewrite_to_0x81`이다.
13. 설정값은 ESP32 Preferences/NVS에 저장하고 재부팅 후 복원한다.
14. Wi-Fi 접속 실패 시 fallback AP 모드로 진입한다.
15. 웹 Dashboard, Wi-Fi 설정, Target Device 설정, Diagnostics 화면을 제공한다.
16. ONVIF는 구현하지 않는다.
17. Pelco-D/P 변환은 구현하지 않는다.
18. 영상 스트리밍 또는 RTSP 기능은 구현하지 않는다.
19. Stop 명령은 우선 처리한다.
20. 최근 RS485 수신 패킷, IP 전송 패킷, 무시된 패킷, 오류 카운터를 웹 화면과 Serial 로그에서 확인할 수 있어야 한다.
