# ESP32 RS485 VISCA to IP VISCA Bridge

## 1. 목적

본 프로젝트는 기존 RS485 방식 PTZ 컨트롤러로 FoMaKo PTZ IP Camera를 제어하기 위한 ESP32 기반 프로토콜 변환 장치를 만드는 것을 목적으로 한다.

기존 PTZ 컨트롤러는 RS485를 통해 VISCA 명령을 출력하고, FoMaKo PTZ 카메라는 IP 네트워크를 통해 IP VISCA 또는 Sony VISCA 제어를 지원한다. 따라서 ESP32는 다음 역할을 수행한다.

```text
PTZ Controller
  → RS485 VISCA
  → ESP32
  → IP VISCA 또는 Sony VISCA over IP
  → FoMaKo PTZ IP Camera
```

즉, PTZ 컨트롤러에서 RS485로 들어오는 VISCA 계열 제어 명령을 ESP32가 수신한 뒤, 이를 IP 기반 VISCA 제어 신호로 변환하여 카메라로 전송한다.

FoMaKo 매뉴얼 기준 주요 제어 프로토콜은 다음과 같다.

| 구분                   | 지원 내용                           |
| ---------------------- | ----------------------------------- |
| 시리얼 제어 인터페이스 | RS232, RS485, RS422                 |
| 시리얼 제어 프로토콜   | VISCA, Pelco-D, Pelco-P             |
| 네트워크 제어 프로토콜 | VISCA OVER IP, IP VISCA, ONVIF      |
| IP VISCA 포트          | 5678                                |
| Sony VISCA 포트        | 52381                               |
| ONVIF 포트             | 2000                                |
| 지원 Baudrate          | 2400, 4800, 9600, 38400, 115200 bps |
| 기본 시리얼 형식       | 8 data bits, 1 stop bit, no parity  |

초기 구현에서는 **IP VISCA 포트 5678**을 우선 사용한다. Sony VISCA 포트 52381은 5678 방식이 동작하지 않거나 호환성 문제가 있을 때 fallback 방식으로 구현한다.

---

## 2. 전체 동작 구조

```text
┌────────────────────┐
│ RS485 PTZ          │
│ Controller         │
└─────────┬──────────┘
          │
          │ RS485 / VISCA
          │
┌─────────▼──────────┐
│ 3.3V RS485 Module  │
│ MAX3485 / SP3485   │
└─────────┬──────────┘
          │ UART2
          │
┌─────────▼──────────┐
│ ESP32              │
│ - VISCA Parser     │
│ - Packet Router    │
│ - IP VISCA Client  │
│ - Web Config       │
└─────────┬──────────┘
          │ Wi-Fi 또는 Ethernet
          │
┌─────────▼──────────┐
│ FoMaKo PTZ Camera  │
│ IP VISCA / Sony    │
└────────────────────┘
```

ESP32는 RS485에서 수신한 VISCA 패킷을 `0xFF` 종료 바이트 기준으로 하나의 명령으로 인식하고, 설정된 대상 카메라 IP와 포트로 UDP 패킷을 전송한다.

---

## 3. 인터페이스 요구사항

## 3.1 ESP32 UART 사용

| 용도                      | ESP32 인터페이스 | 권장 핀                    | 비고            |
| ------------------------- | ---------------- | -------------------------- | --------------- |
| PC 연결 / 디버깅 / 업로드 | UART0 / Serial   | TX0, RX0                   | USB Serial 전용 |
| RS485 수신/송신           | UART2 / Serial2  | RX2 = GPIO16, TX2 = GPIO17 | RS485 모듈 연결 |
| RS485 방향 제어           | GPIO             | GPIO4 권장                 | DE, /RE 제어    |

중요 사항:

- TX0, RX0는 컴퓨터와 연결되어 디버깅 및 펌웨어 업로드 용도로 사용한다.
- RS485 모듈은 TX2, RX2에 연결한다.
- RS485를 TX0, RX0에 연결하지 않는다.
- 나머지 GPIO는 상태 LED, 설정 버튼, AP 모드 진입 버튼 등에 추후 할당한다.

## 3.2 RS485 모듈

ESP32는 3.3V TTL 로직이므로 RS485 변환 모듈도 3.3V 로직 호환 제품을 사용한다.

권장 RS485 칩:

| 칩        | 설명                               |
| --------- | ---------------------------------- |
| MAX3485   | ESP32에 적합한 3.3V RS485 트랜시버 |
| SP3485    | ESP32에 적합한 3.3V RS485 트랜시버 |
| ADM3485   | 3.3V RS485 트랜시버                |
| MAX13487E | 자동 방향 제어 가능 RS485 트랜시버 |

일반 MAX485 모듈은 5V Arduino용인 경우가 많으므로 ESP32 RX 핀에 5V 신호가 들어가지 않도록 주의한다.

## 3.3 RS485 배선

| RS485 모듈 핀 | ESP32 핀                 |
| ------------- | ------------------------ |
| VCC           | 3V3                      |
| GND           | GND                      |
| RO            | GPIO16 / RX2             |
| DI            | GPIO17 / TX2             |
| DE            | GPIO4                    |
| /RE           | GPIO4                    |
| A 또는 +      | PTZ 컨트롤러 RS485 A / + |
| B 또는 -      | PTZ 컨트롤러 RS485 B / - |

초기 단방향 브리지에서는 ESP32가 주로 RS485 데이터를 수신하므로 기본 상태는 수신 모드로 둔다.

```text
DE = LOW
/RE = LOW
```

DE와 /RE를 하나의 GPIO에 묶어 사용하는 경우:

| GPIO 상태 | 동작      |
| --------- | --------- |
| LOW       | 수신 모드 |
| HIGH      | 송신 모드 |

---

## 4. 프로토콜 요구사항

## 4.1 입력: RS485 VISCA

PTZ 컨트롤러는 RS485를 통해 VISCA 명령을 전송한다. VISCA 명령은 바이너리 패킷이며 일반적으로 `0xFF`로 종료된다.

예시:

```text
81 01 06 04 FF
```

ESP32는 UART2로 수신한 바이트를 버퍼에 저장하다가 `0xFF`를 만나면 하나의 VISCA 패킷으로 처리한다.

## 4.2 출력: IP VISCA

초기 구현은 Raw VISCA payload를 UDP 패킷으로 FoMaKo 카메라의 IP VISCA 포트에 그대로 전송한다.

```text
RS485 입력:
81 01 06 04 FF

UDP 출력:
81 01 06 04 FF

전송 대상:
카메라 IP:5678
```

## 4.3 Sony VISCA fallback

FoMaKo 카메라는 Sony VISCA 포트 52381도 제공한다. 5678 포트의 Raw IP VISCA 방식이 동작하지 않으면 Sony VISCA over IP framing을 구현한다.

Sony VISCA fallback은 초기 필수 기능이 아니며, 다음 단계에서 구현한다.

## 4.4 ONVIF 제외

ONVIF는 HTTP/SOAP/XML 기반의 IP 카메라 표준 제어 프로토콜이므로 초기 구현 범위에서 제외한다.

초기 목표는 PTZ 제어 브리지이며, 영상 스트리밍, ONVIF 검색, ONVIF 인증, RTSP 주소 획득 기능은 구현하지 않는다.

---

## 5. 기능 요구사항

## 5.1 RS485 VISCA 수신

펌웨어는 다음 기능을 수행해야 한다.

1. UART2를 설정된 Baudrate로 초기화한다.
2. RS485에서 들어오는 바이트를 지속적으로 읽는다.
3. 수신 바이트를 VISCA 버퍼에 저장한다.
4. `0xFF`를 수신하면 하나의 패킷으로 판단한다.
5. 최소 패킷 길이를 검사한다.
6. 유효한 패킷이면 IP VISCA 전송 모듈로 넘긴다.
7. 전송 후 버퍼를 초기화한다.
8. 버퍼 오버플로우가 발생하면 버퍼를 초기화하고 오류 카운터를 증가시킨다.

권장 버퍼 크기:

```text
128 bytes
```

권장 패킷 timeout:

```text
50 ms
```

## 5.2 IP VISCA 전송

펌웨어는 완성된 VISCA 패킷을 설정된 대상 카메라로 전송해야 한다.

초기 구현:

| 항목      | 값                               |
| --------- | -------------------------------- |
| 전송 방식 | UDP                              |
| 기본 포트 | 5678                             |
| Payload   | RS485에서 받은 VISCA 패킷 그대로 |

전송 실패가 발생하더라도 펌웨어가 멈추면 안 되며, 오류 카운터만 증가시킨다.

## 5.3 USB Serial 디버그

USB Serial은 디버그 및 모니터링 용도로만 사용한다.

예시 로그:

```text
WiFi connected: 192.168.1.50
Target camera: 192.168.1.100:5678
RS485 RX: 81 01 06 04 FF
IP VISCA TX: 81 01 06 04 FF
```

디버그 출력은 RS485 통신 UART와 분리되어야 한다.

---

## 6. 설정 및 저장 요구사항

모든 설정값은 ESP32 Flash에 저장되어야 하며, ESP32가 재시작될 때 저장된 값을 읽어와야 한다.

ESP32 Arduino 환경에서는 `Preferences` 또는 NVS를 사용한다.

저장해야 할 설정값:

| 설정 항목           | 설명                     | 기본값    |
| ------------------- | ------------------------ | --------- |
| Wi-Fi SSID          | 접속할 무선 AP 이름      | 없음      |
| Wi-Fi Password      | 무선 AP 비밀번호         | 없음      |
| RS485 Baudrate      | PTZ 컨트롤러 Baudrate    | 9600      |
| RS485 RX Pin        | UART2 RX 핀              | GPIO16    |
| RS485 TX Pin        | UART2 TX 핀              | GPIO17    |
| RS485 DE/RE Pin     | 방향 제어 핀             | GPIO4     |
| Protocol Mode       | IP VISCA 또는 Sony VISCA | IP VISCA  |
| Default Camera Port | 카메라 제어 포트         | 5678      |
| Target Device List  | PTZ 카메라 목록          | 비어 있음 |

---

## 7. Target Device 관리 요구사항

웹 설정 화면에서 Target device, 즉 PTZ 카메라를 추가, 수정, 삭제할 수 있어야 한다.

각 Target device는 다음 정보를 가진다.

| 항목               | 설명                                | 예시          |
| ------------------ | ----------------------------------- | ------------- |
| Device Name        | 카메라 이름                         | CAM1          |
| RS485 VISCA ID     | PTZ 컨트롤러에서 사용하는 카메라 ID | 1             |
| VISCA Address Byte | VISCA 명령 첫 바이트                | 0x81          |
| Camera IP          | 대상 PTZ 카메라 IP                  | 192.168.1.101 |
| Camera Port        | IP VISCA 또는 Sony VISCA 포트       | 5678          |
| Protocol Mode      | IP VISCA / Sony VISCA               | IP VISCA      |
| Enabled            | 사용 여부                           | true          |

VISCA 주소 매핑 예시는 다음과 같다.

| RS485 VISCA ID | VISCA Address Byte | Target Camera IP | Port |
| -------------: | ------------------ | ---------------- | ---: |
|              1 | 0x81               | 192.168.1.101    | 5678 |
|              2 | 0x82               | 192.168.1.102    | 5678 |
|              3 | 0x83               | 192.168.1.103    | 5678 |

브로드캐스트 주소 `0x88`은 설정에 따라 모든 카메라에 전송하거나 무시할 수 있도록 한다.

---

## 8. 디버그 및 모니터링용 웹 화면

ESP32는 간단한 웹 기반 설정 및 모니터링 화면을 제공해야 한다.

## 8.1 기본 화면

기본 화면에는 다음 정보를 표시한다.

| 항목                    | 설명                  |
| ----------------------- | --------------------- |
| ESP32 IP                | 현재 ESP32의 IP 주소  |
| Wi-Fi 연결 상태         | 연결됨 / 연결 안 됨   |
| 현재 SSID               | 접속 중인 AP 이름     |
| RS485 Baudrate          | 현재 RS485 통신 속도  |
| Protocol Mode           | IP VISCA / Sony VISCA |
| 등록된 Target Device 수 | 카메라 목록 개수      |
| 마지막 RS485 수신 패킷  | HEX 문자열            |
| 마지막 IP 전송 패킷     | HEX 문자열            |
| RS485 RX Count          | 수신 패킷 수          |
| IP TX Count             | 전송 패킷 수          |
| Error Count             | 오류 카운터           |

## 8.2 Wi-Fi 설정 화면

다음 값을 설정할 수 있어야 한다.

| 항목           | 설명                     |
| -------------- | ------------------------ |
| SSID           | 무선 AP 이름             |
| Password       | 무선 AP 비밀번호         |
| DHCP 사용 여부 | DHCP 또는 Static IP 선택 |
| Static IP      | 고정 IP 사용 시 ESP32 IP |
| Gateway        | 고정 IP Gateway          |
| Subnet Mask    | 고정 IP Subnet           |

Wi-Fi 접속 실패 시 ESP32는 fallback AP 모드로 진입한다.

## 8.3 Target Device 설정 화면

다음 기능을 제공한다.

- Target device 추가
- Target device 수정
- Target device 삭제
- Device별 RS485 VISCA ID 할당
- Device별 IP 주소 설정
- Device별 포트 설정
- Device별 프로토콜 모드 설정
- Device 활성화/비활성화

## 8.4 진단 화면

다음 정보를 표시한다.

| 항목                 | 설명                        |
| -------------------- | --------------------------- |
| 최근 RS485 수신 로그 | 최근 수신된 VISCA 패킷 목록 |
| 최근 IP 전송 로그    | 최근 전송된 UDP 패킷 목록   |
| 잘못된 패킷 수       | malformed packet count      |
| 버퍼 오버플로우 수   | buffer overflow count       |
| Wi-Fi 재접속 횟수    | reconnect count             |
| Uptime               | ESP32 동작 시간             |

---

## 9. 개발 단계

## 9.1 1단계: 최소 기능 구현

목표는 RS485로 들어온 VISCA 명령을 그대로 IP VISCA 포트 5678로 전송하는 것이다.

필수 기능:

1. Wi-Fi 연결
2. UART2 RS485 수신
3. `0xFF` 기준 VISCA 패킷 검출
4. UDP로 카메라 IP:5678 전송
5. USB Serial에 HEX 로그 출력

## 9.2 2단계: 설정 저장

다음 값을 Flash에 저장하고 재부팅 후 복원한다.

- Wi-Fi SSID
- Wi-Fi Password
- RS485 Baudrate
- Target Camera IP
- Target Camera Port
- Protocol Mode

## 9.3 3단계: 웹 설정 화면

웹 화면에서 Wi-Fi 및 Target device 설정을 변경할 수 있게 한다.

## 9.4 4단계: 다중 카메라 라우팅

VISCA 명령의 첫 바이트를 확인하여 대상 카메라 IP로 라우팅한다.

예:

```text
0x81 → CAM1 → 192.168.1.101:5678
0x82 → CAM2 → 192.168.1.102:5678
0x83 → CAM3 → 192.168.1.103:5678
```

## 9.5 5단계: 응답 처리

필요 시 VISCA ACK / Completion 응답을 RS485 컨트롤러로 반환한다.

FoMaKo 매뉴얼의 VISCA 응답 형식:

| 응답       | 패킷       | 의미           |
| ---------- | ---------- | -------------- |
| ACK        | `z0 41 FF` | 명령 수락      |
| Completion | `z0 51 FF` | 명령 실행 완료 |

`z`는 camera address + 8이다.

초기에는 응답 처리를 구현하지 않아도 된다. 컨트롤러가 응답을 요구하는 경우 다음 방식 중 하나를 선택한다.

| 방식      | 설명                             |
| --------- | -------------------------------- |
| none      | 응답 없음                        |
| synthetic | ESP32가 가짜 ACK/Completion 생성 |
| forward   | 카메라 응답을 받아 RS485로 전달  |

---

## 10. 초기 구현에서 제외할 항목

초기 버전에서는 다음 기능을 구현하지 않는다.

| 제외 항목          | 이유                                |
| ------------------ | ----------------------------------- |
| ONVIF 제어         | 구현 복잡도가 높음                  |
| 영상 스트리밍      | PTZ 제어 브리지 목적과 다름         |
| RTSP 처리          | 제어 기능과 무관                    |
| Pelco-D/P 변환     | 현재 목표는 VISCA 변환              |
| Sony VISCA framing | 5678 Raw IP VISCA 실패 시 후속 구현 |
| 완전한 보안 인증   | 내부망 사용 전제                    |

---

## 11. 안정성 요구사항

실사용을 위해 다음 안정성 기능을 포함한다.

| 항목                 | 요구사항                               |
| -------------------- | -------------------------------------- |
| Wi-Fi 재접속         | 연결이 끊어지면 자동 재접속            |
| Watchdog             | 펌웨어 멈춤 방지                       |
| Packet timeout       | 불완전한 VISCA 패킷 자동 폐기          |
| Buffer overflow 처리 | 버퍼 초기화 및 오류 카운터 증가        |
| Stop 명령 우선 처리  | PTZ 정지 명령 누락 방지                |
| 설정값 복구          | 재부팅 후 Flash 저장값 로드            |
| AP fallback          | Wi-Fi 접속 실패 시 설정용 AP 모드 진입 |

PTZ 제어에서 가장 중요한 것은 Stop 명령이 누락되지 않는 것이다. Pan/Tilt/Zoom Stop 명령은 가능한 한 즉시 전송해야 한다.

---

## 12. 테스트 기준

프로젝트 성공 기준은 다음과 같다.

| 테스트            | 기대 결과                                |
| ----------------- | ---------------------------------------- |
| ESP32 Wi-Fi 연결  | ESP32가 IP를 할당받음                    |
| 웹 화면 접속      | 브라우저에서 ESP32 설정 화면 접근 가능   |
| RS485 수신        | PTZ 컨트롤러 조작 시 VISCA HEX 패킷 수신 |
| IP VISCA 전송     | 수신한 패킷이 카메라 IP:5678로 전송됨    |
| Pan/Tilt 조작     | 카메라가 조이스틱 방향대로 움직임        |
| Stop 조작         | 조이스틱을 놓으면 카메라가 멈춤          |
| Zoom 조작         | Zoom In/Out 동작                         |
| Preset 호출       | 저장된 preset으로 이동                   |
| 설정 저장         | 재부팅 후 설정값 유지                    |
| 다중 카메라       | RS485 ID별로 다른 IP 카메라 제어         |
| USB Serial 디버그 | RS485와 독립적으로 로그 확인 가능        |

---

## 13. 구현 지시사항

이 프로젝트는 Arduino ESP32 펌웨어로 구현한다.

우선 다음 최소 기능부터 구현한다.

1. ESP32를 Wi-Fi에 연결한다.
2. UART2를 사용하여 RS485 VISCA 패킷을 수신한다.
3. VISCA 패킷은 `0xFF`를 기준으로 구분한다.
4. 수신한 VISCA 패킷을 UDP payload로 `CAMERA_IP:5678`에 그대로 전송한다.
5. 모든 수신/전송 패킷을 USB Serial에 HEX 형식으로 출력한다.
6. RS485에는 UART0를 사용하지 않는다.
7. UART0는 USB Serial 디버깅 전용으로만 사용한다.
8. ONVIF는 구현하지 않는다.
9. Pelco-D/P 변환은 구현하지 않는다.
10. 코드 구조는 추후 웹 설정, 다중 카메라, Sony VISCA fallback을 추가하기 쉽도록 모듈화한다.

권장 소스 구조:

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
  DeviceConfig.h
  DeviceConfig.cpp
  WebConfigServer.h
  WebConfigServer.cpp
```