# ESP32 RS485 VISCA to IP VISCA Gateway 요구사항

## 1. 프로젝트 목적

기존 **RS485 VISCA PTZ 컨트롤러**에서 나오는 VISCA 명령을 ESP32가 수신한 뒤, 네트워크상의 **FoMaKo PTZ IP 카메라**로 IP VISCA 명령을 전달하는 게이트웨이를 만든다.

```text
RS485 PTZ Controller
  → RS485 VISCA
  → ESP32 Gateway
  → IP VISCA / Sony VISCA over IP
  → FoMaKo PTZ IP Camera
```

현재 요구사항에서는 **AP 모드나 웹 설정 서버는 중요하지 않다.**  
초기 구현과 설정은 모두 **USB Serial 메뉴**를 통해 수행한다고 가정한다.

---

## 2. FoMaKo 카메라 관련 정보

FoMaKo 매뉴얼 기준 확인된 제어 정보는 다음과 같다.

| 항목                   | 내용                                    |
| ---------------------- | --------------------------------------- |
| 시리얼 제어 인터페이스 | RS232, RS485, RS422                     |
| 시리얼 제어 프로토콜   | VISCA, Pelco-D, Pelco-P                 |
| 네트워크 제어 프로토콜 | VISCA OVER IP, IP VISCA, ONVIF 등       |
| IP VISCA 포트          | 5678                                    |
| Sony VISCA 포트        | 52381                                   |
| ONVIF 포트             | 2000                                    |
| 지원 Baudrate          | 2400, 4800, 9600, 38400, 115200 bps     |
| 시리얼 형식            | 8 data bits, 1 stop bit, no parity, 8N1 |

본 프로젝트의 기본 제어 방식은 다음과 같다.

| 우선순위 | 방식             |  포트 | 설명                                          |
| -------: | ---------------- | ----: | --------------------------------------------- |
|        1 | IP_VISCA_RAW_UDP |  5678 | Raw VISCA payload를 UDP로 전송                |
|        2 | IP_VISCA_RAW_TCP |  5678 | UDP가 동작하지 않을 때 선택 가능한 fallback   |
|        3 | SONY_VISCA_UDP   | 52381 | Sony VISCA over IP framing이 필요한 경우 사용 |
|     제외 | ONVIF            |  2000 | 본 프로젝트에서는 구현하지 않음               |

주의: 매뉴얼에는 IP VISCA 포트는 명시되어 있지만 UDP/TCP 여부는 명확히 적혀 있지 않다. 기본값은 UDP로 두되, 설정에서 TCP 및 Sony VISCA 모드를 선택할 수 있게 한다.

---

## 3. 하드웨어 구성

## 3.1 ESP32 UART 사용

| 용도                   | ESP32 인터페이스 | 권장 핀                    | 설명                              |
| ---------------------- | ---------------- | -------------------------- | --------------------------------- |
| USB Serial 디버그/설정 | UART0 / Serial   | TX0, RX0                   | PC와 연결, 메뉴 출력 및 설정 입력 |
| RS485 VISCA 수신/송신  | UART2 / Serial2  | RX2 = GPIO16, TX2 = GPIO17 | RS485 모듈 연결                   |
| RS485 방향 제어        | GPIO             | GPIO4                      | DE, /RE 제어                      |

중요 사항:

- UART0는 USB Serial 전용으로 사용한다.
- RS485는 UART2에 연결한다.
- RS485를 TX0/RX0에 연결하지 않는다.
- TX0/RX0를 RS485와 공유하면 업로드, 디버그, RS485 통신이 충돌할 수 있다.

## 3.2 RS485 모듈

ESP32는 3.3V TTL 로직이므로 RS485 모듈도 3.3V TTL 호환 제품을 사용한다.

권장 칩:

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

DE와 /RE를 하나의 GPIO에 묶어 사용하는 경우:

| GPIO 상태 | RS485 동작          |
| --------- | ------------------- |
| LOW       | Receive, 수신 모드  |
| HIGH      | Transmit, 송신 모드 |

기본 상태는 수신 모드이다.

```text
DE = LOW
/RE = LOW
```

ESP32는 평소에는 RS485 명령을 들어야 하므로 Receive 상태를 유지한다. 카메라 응답 또는 synthetic 응답을 RS485 컨트롤러로 되돌려 보낼 때만 잠깐 Transmit 상태로 전환하고, 전송 완료 후 즉시 Receive 상태로 복귀한다.

## 3.4 상태 LED

| 항목      | 값                          |
| --------- | --------------------------- |
| GPIO      | GPIO2                       |
| 용도      | Wi-Fi 연결 상태 / RS485 신호 수신 표시 |

동작 방식 (`StatusLed` 클래스, [StatusLed.h](src/StatusLed.h) / [StatusLed.cpp](src/StatusLed.cpp)):

| 상황                     | LED 동작                                  |
| ------------------------ | ------------------------------------------ |
| Wi-Fi 연결됨             | 항상 켜짐(고정 ON)                         |
| Wi-Fi 연결 안 됨         | 1초 간격으로 깜박임                        |
| RS485 VISCA 패킷 수신    | 0.2초 간격으로 2회 깜박인 뒤 위 상태로 복귀 |

RS485 패킷 수신 표시가 Wi-Fi 상태 표시보다 우선하며, 2회 깜박임이 끝나면 그 시점의 Wi-Fi 연결 여부에 따라 원래 패턴(고정 ON 또는 1초 깜박임)으로 자동 복귀한다. `delay()`를 쓰지 않는 `millis()` 기반 non-blocking 상태 머신이라 RS485 수신/IP 전송을 막지 않는다.

GPIO2는 상태 LED 전용으로 예약되어 있어, Serial 메뉴의 RS485 RX/TX/DE-RE Pin 설정에서 GPIO2를 입력하면 "reserved for the status LED" 오류로 거부된다.

---

## 4. VISCA 주소와 라우팅 기본 개념

VISCA 명령은 일반적으로 `0xFF`로 끝난다. 첫 바이트는 카메라 번호를 나타낸다.

| 카메라 번호 | VISCA 첫 바이트 |
| ----------: | --------------- |
|           1 | 0x81            |
|           2 | 0x82            |
|           3 | 0x83            |
|           4 | 0x84            |
|           5 | 0x85            |
|           6 | 0x86            |
|           7 | 0x87            |
|   Broadcast | 0x88            |

RS485 VISCA에서는 `0x81~0x87`로 1~7번 카메라를 구분한다.  
VISCA over IP에서는 실제 대상 카메라는 **IP 주소와 포트**로 선택된다. 따라서 ESP32는 RS485에서 받은 카메라 번호를 라우팅 테이블의 IP 주소로 매핑한다.

예:

```text
RS485 input:
85 01 06 04 FF

라우팅:
CAM5 → 192.168.1.105:5678

IP output:
81 01 06 04 FF
```

IP 카메라로 보낼 때는 기본적으로 첫 바이트를 `0x81`로 바꾼다. 이유는 IP 주소로 이미 대상 카메라가 정해졌기 때문에, IP 카메라는 자기 자신을 1번 카메라로 받는 것이 가장 안전하기 때문이다.

---

## 5. 라우팅 정책

디바이스 또는 Target Device 같은 별도 추상 개념을 쓰지 않는다.  
**카메라 1~7번 고정 슬롯**만 사용한다.

각 카메라 번호는 다음 설정값을 가진다.

| 필드          | 설명                      | 기본값           |
| ------------- | ------------------------- | ---------------- |
| Camera Number | 1~7                       | 고정             |
| VISCA Address | 0x81~0x87                 | 고정             |
| Camera IP     | 대상 IP 카메라 주소       | 비어 있음        |
| Port          | IP VISCA 포트             | 5678             |
| Protocol      | 전송 프로토콜             | IP_VISCA_RAW_UDP |
| Address Mode  | IP 전송 시 첫 바이트 처리 | rewrite_0x81     |
| Enabled       | 사용 여부                 | IP가 있으면 사용 |

라우팅 동작은 다음과 같다.

| RS485 입력       | 조건                              | 동작                           |
| ---------------- | --------------------------------- | ------------------------------ |
| 0x81~0x87        | 해당 카메라 번호에 IP가 설정됨    | 해당 IP 카메라로 변환 전달     |
| 0x81~0x87        | 해당 카메라 번호에 IP가 비어 있음 | 무시                           |
| 0x88 Broadcast   | IP가 설정된 카메라가 있음         | IP가 설정된 모든 카메라로 전송 |
| 0x88 Broadcast   | IP가 설정된 카메라가 없음         | 무시                           |
| 기타 시작 바이트 | 유효하지 않음                     | Malformed 처리                 |

## 5.1 Broadcast 처리

Broadcast `0x88` 명령이 들어오면, IP가 설정된 모든 카메라에 명령을 보낸다.

예를 들어 CAM2, CAM5, CAM7에 IP가 설정되어 있다면:

```text
RS485 Broadcast input:
88 01 00 01 FF

IP output:
CAM2 → 192.168.1.102:5678 | 81 01 00 01 FF
CAM5 → 192.168.1.105:5678 | 81 01 00 01 FF
CAM7 → 192.168.1.107:5678 | 81 01 00 01 FF
```

Broadcast도 기본적으로 `rewrite_0x81` 정책을 적용한다.

## 5.2 Address Mode

| Address Mode   | 설명                                               |
| -------------- | -------------------------------------------------- |
| rewrite_0x81   | IP 카메라로 보낼 때 첫 바이트를 항상 `0x81`로 변경 |
| preserve       | RS485에서 받은 첫 바이트를 그대로 유지             |
| rewrite_by_cam | 카메라 번호에 맞춰 `0x81~0x87`로 변경              |

기본값은 `rewrite_0x81`이다.

---

## 6. RS485 VISCA 패킷 처리

펌웨어는 다음 방식으로 VISCA 패킷을 처리한다.

1. UART2에서 바이트를 계속 읽는다.
2. 바이트를 수신 버퍼에 저장한다.
3. `0xFF`를 만나면 하나의 VISCA 패킷으로 확정한다.
4. 최소 길이와 시작 바이트를 검사한다.
5. 첫 바이트로 카메라 번호 또는 Broadcast 여부를 판별한다.
6. 라우팅 테이블에서 해당 카메라 번호의 IP 설정 여부를 확인한다.
7. IP가 있으면 주소 변환 후 IP VISCA로 전송한다.
8. IP가 없으면 무시한다.
9. Broadcast면 IP가 설정된 모든 카메라에 전송한다.
10. 처리 후 버퍼를 초기화한다.

권장값:

| 항목              | 값        |
| ----------------- | --------- |
| VISCA buffer size | 128 bytes |
| Packet timeout    | 50 ms     |
| 기본 Baudrate     | 9600 bps  |
| 시리얼 형식       | 8N1       |

오류 처리:

| 오류             | 처리                                         |
| ---------------- | -------------------------------------------- |
| Buffer Overflow  | 버퍼 초기화, Buffer Overflow 카운터 증가     |
| Packet Timeout   | 불완전 패킷 폐기, Packet Timeout 카운터 증가 |
| Malformed Packet | 패킷 폐기, Malformed Packet 카운터 증가      |

---

## 7. IP VISCA 전송

기본 전송 방식은 UDP 기반 Raw IP VISCA이다.

| 항목     | 기본값                 |
| -------- | ---------------------- |
| Protocol | IP_VISCA_RAW_UDP       |
| Port     | 5678                   |
| Payload  | 주소 변환된 VISCA 패킷 |

예:

```text
RS485 input:
85 01 06 04 FF

Address Mode:
rewrite_0x81

IP output:
81 01 06 04 FF

Target:
192.168.1.105:5678
```

전송 실패 시 펌웨어는 멈추지 않고 `IP TX Failed` 카운터만 증가시킨다.

---

## 8. 응답 처리

응답 처리는 설정으로 선택 가능해야 한다.

FoMaKo 매뉴얼의 VISCA 응답 형식:

| 응답       | 패킷       | 의미           |
| ---------- | ---------- | -------------- |
| ACK        | `z0 41 FF` | 명령 수락      |
| Completion | `z0 51 FF` | 명령 실행 완료 |

`z = camera address + 8`이다.

지원할 응답 모드:

| Response Mode   | 설명                                                      |
| --------------- | --------------------------------------------------------- |
| none            | RS485 컨트롤러로 응답을 보내지 않음                       |
| synthetic       | ESP32가 ACK/Completion을 생성해서 RS485로 보냄            |
| forward         | IP 카메라 응답을 받아 RS485로 전달                        |
| forward_rewrite | IP 카메라 응답을 RS485 쪽 카메라 번호에 맞게 보정 후 전달 |

RS485로 응답을 보낼 때는 반드시 송신 모드로 전환한다.

```cpp
digitalWrite(RS485_DIR_PIN, HIGH);  // Transmit
RS485.write(response, len);
RS485.flush();
digitalWrite(RS485_DIR_PIN, LOW);   // Receive
```

초기 기본값은 `none` 또는 `synthetic` 중 선택 가능하게 한다. 컨트롤러가 응답 없이도 동작하면 `none`이 가장 단순하다.

---

## 9. Stop 명령 우선 처리

PTZ 제어에서 가장 중요한 것은 Stop 명령 누락 방지이다.  
Pan/Tilt/Zoom Stop 명령은 가능한 즉시 IP 카메라로 전송해야 한다.

대표 Stop 명령:

| 기능          | VISCA 명령 예시              |
| ------------- | ---------------------------- |
| Pan/Tilt Stop | `81 01 06 01 00 00 03 03 FF` |
| Zoom Stop     | `81 01 04 07 00 FF`          |

요구사항:

- Stop 명령은 전송 큐에서 우선 처리한다.
- Wi-Fi가 끊겨 있으면 실패 카운터를 증가시키고 로그를 남긴다.
- Stop 명령이 버퍼 대기나 로그 출력 때문에 지연되지 않도록 한다.

---

## 10. 설정 저장

모든 설정값은 ESP32 Flash에 저장되어야 한다. Arduino ESP32 환경에서는 `Preferences` 또는 NVS를 사용한다.

저장해야 할 설정값:

| 설정 항목             | 설명                     | 기본값                                         |
| --------------------- | ------------------------ | ---------------------------------------------- |
| Wi-Fi SSID            | 접속할 AP 이름           | 없음                                           |
| Wi-Fi Password        | AP 비밀번호              | 없음                                           |
| IP Mode               | DHCP 또는 Static         | DHCP                                           |
| Static IP             | 고정 IP 사용 시 ESP32 IP | 없음                                           |
| Gateway               | Gateway                  | 없음                                           |
| Subnet Mask           | Subnet                   | 없음                                           |
| RS485 Baudrate        | RS485 속도               | 9600                                           |
| RS485 RX Pin          | UART2 RX                 | GPIO16                                         |
| RS485 TX Pin          | UART2 TX                 | GPIO17                                         |
| RS485 DE/RE Pin       | 방향 제어 핀             | GPIO4                                          |
| Camera 1 IP           | CAM1 IP                  | empty                                          |
| Camera 1 Port         | CAM1 Port                | 5678                                           |
| Camera 1 Protocol     | CAM1 Protocol            | IP_VISCA_RAW_UDP                               |
| Camera 1 Address Mode | CAM1 Address Mode        | rewrite_0x81                                   |
| Camera 2~7 설정       | 위와 동일                | empty / 5678 / IP_VISCA_RAW_UDP / rewrite_0x81 |
| Response Mode         | 응답 처리 모드           | none                                           |
| Debug Mode            | 디버그 출력 여부         | off                                            |

---

## 11. Serial 시작 메뉴

전원 투입 후 USB Serial에 다음과 같은 메뉴를 출력한다.

```text
============================================================
 ESP32 RS485 VISCA to IP VISCA Gateway
 Firmware : v1.0.0
============================================================

[STATUS]
  Mode             : Gateway Running
  Wi-Fi            : Connected / Disconnected
  ESP32 IP         : 192.168.1.50 또는 Not assigned
  Debug Mode       : OFF

------------------------------------------------------------
 Main Menu
------------------------------------------------------------
  1. Network Settings
  2. RS485 Settings
  3. Routing Table
  4. Counters
  5. Debug Mode

Select menu number:
============================================================
```

한글 Serial 출력은 환경에 따라 깨질 수 있으므로 기본 메뉴는 영문으로 출력한다.

---

## 12. Serial 메뉴 상세

## 12.1 Network Settings

```text
============================================================
 1. Network Settings
============================================================

  Wi-Fi Mode       : STA
  Wi-Fi Status     : Connected
  SSID             : Your_AP_Name
  DHCP             : Enabled
  ESP32 IP         : 192.168.1.50
  Gateway          : 192.168.1.1
  Subnet           : 255.255.255.0

------------------------------------------------------------
 Options
------------------------------------------------------------
  1. Set Wi-Fi SSID
  2. Set Wi-Fi Password
  3. Set DHCP / Static IP
  4. Retry Wi-Fi Connection
  0. Back to Main Menu
```

Network Settings의 값(SSID, 비밀번호, DHCP/Static IP, Gateway, Subnet)은 별도의 저장 단계 없이
변경 즉시 flash(NVS)에 기록된다.

Wi-Fi 연결 실패 시 자동 AP 모드로 가지 않는다.  
Serial 메뉴는 계속 사용 가능해야 하며, ESP32는 주기적으로 Wi-Fi 재접속을 시도한다.

Wi-Fi 연결 실패 시 출력:

```text
[NETWORK]
  Wi-Fi Status : Disconnected
  ESP32 IP     : Not assigned

[WARN] Wi-Fi connection failed.
[INFO] IP forwarding is unavailable until network is restored.
[INFO] Use Serial menu to update Wi-Fi settings.
```

AP 모드는 본 요구사항의 핵심이 아니므로 구현하지 않아도 된다.

### 12.1.1 Set DHCP / Static IP

Network Settings의 "3. Set DHCP / Static IP"를 선택하면 진입하는 서브 메뉴.

```text
============================================================
 1.3 Set DHCP / Static IP
============================================================

  Current Mode     : DHCP

------------------------------------------------------------
 Options
------------------------------------------------------------
  1. Use DHCP
  2. Configure Static IP
  0. Back to Network Settings
```

"2. Configure Static IP"를 선택하면 현재 저장된 IP/Gateway/Subnet Mask 값을 보여주고,
항목별로 개별 수정할 수 있는 서브 메뉴로 진입한다 (선택 시 useDhcp는 즉시 false로 전환되고 flash에 저장됨).

```text
============================================================
 1.3.2 Configure Static IP
============================================================

  IP Address       : 192.168.1.50
  Gateway          : 192.168.1.1
  Subnet Mask      : 255.255.255.0

------------------------------------------------------------
 Options
------------------------------------------------------------
  1. Set IP Address
  2. Set Gateway
  3. Set Subnet Mask
  0. Back to DHCP / Static IP menu
```

각 항목은 값을 입력하는 즉시 flash에 저장되며, 입력 후에는 다시 이 메뉴로 돌아온다.
별도의 저장 메뉴는 없다.

## 12.2 RS485 Settings

```text
============================================================
 2. RS485 Settings
============================================================

  UART Port        : UART2 / Serial2
  RX Pin           : GPIO16
  TX Pin           : GPIO17
  DE/RE Pin        : GPIO4
  Baudrate         : 9600
  Format           : 8N1
  Default Mode     : Receive

------------------------------------------------------------
 Options
------------------------------------------------------------
  1. Set Baudrate
  2. Set RX Pin
  3. Set TX Pin
  4. Set DE/RE Pin
  0. Back to Main Menu
```

Baudrate/RX Pin/TX Pin/DE-RE Pin은 값을 입력하는 즉시 flash에 저장되고 UART2에 재적용되며,
별도의 저장 메뉴는 없다.

Baudrate 선택값:

```text
1. 2400
2. 4800
3. 9600
4. 38400
5. 115200
```

RX/TX/DE-RE 핀 입력 시 유효성 검사:

- 아무 값도 입력하지 않고 Enter만 누르면 변경 없이 취소된다.
- **GPIO1, 3** : UART0(USB Serial 메뉴 전용)이라 사용 불가
- **GPIO6~11** : 보드 내장 SPI Flash 전용이라 사용 불가
- **GPIO34~39** : 입력 전용이라 RX는 가능하지만 TX/DE-RE(출력 필요)로는 사용 불가
- **GPIO0, 2, 5, 12, 15** (부팅 스트래핑 핀) : 사용은 가능하나 외부 배선에 따라 부팅에 영향을 줄 수 있어 설정 시 경고 메시지가 출력됨
- 위 조건에 걸리면 에러 메시지와 함께 다시 입력받으며, 이때도 빈 입력으로 취소할 수 있다.

## 12.3 Routing Table

```text
============================================================
 3. Routing Table
============================================================

  Rule:
    Camera 1~7 : Forward if IP is configured
    No IP      : Ignore
    Broadcast  : Send to all configured cameras

------------------------------------------------------------
 Routing Table
------------------------------------------------------------
  CAM | VISCA | Camera IP       | Port | Protocol          | Addr Mode
  ----+-------+-----------------+------+-------------------+--------------
   1  | 0x81  | -               | 5678 | IP_VISCA_RAW_UDP  | rewrite_0x81
   2  | 0x82  | -               | 5678 | IP_VISCA_RAW_UDP  | rewrite_0x81
   3  | 0x83  | -               | 5678 | IP_VISCA_RAW_UDP  | rewrite_0x81
   4  | 0x84  | -               | 5678 | IP_VISCA_RAW_UDP  | rewrite_0x81
   5  | 0x85  | 192.168.1.105   | 5678 | IP_VISCA_RAW_UDP  | rewrite_0x81
   6  | 0x86  | -               | 5678 | IP_VISCA_RAW_UDP  | rewrite_0x81
   7  | 0x87  | -               | 5678 | IP_VISCA_RAW_UDP  | rewrite_0x81

  Broadcast 0x88 : forward to all cameras with IP configured

------------------------------------------------------------
 Options
------------------------------------------------------------
  1. Select Camera
  0. Back to Main Menu
```

"1. Select Camera"를 선택하고 카메라 번호(1~7)를 입력하면 해당 카메라 전용 서브 메뉴로 진입한다.

```text
============================================================
 3.5 CAM5
============================================================

  VISCA Address    : 0x85
  Camera IP        : 192.168.1.105
  Port             : 5678
  Protocol         : IP_VISCA_RAW_UDP
  Address Mode     : rewrite_0x81

------------------------------------------------------------
 Options
------------------------------------------------------------
  1. Set Camera IP
  2. Clear Camera IP
  3. Set Camera Port
  4. Set Protocol
  5. Set Address Mode
  0. Back to Routing Table
```

각 항목은 값을 입력하는 즉시 flash에 저장되며, 별도의 저장 메뉴는 없다.
"3. Set Camera Port" 입력 시 아무 값도 입력하지 않고 Enter만 누르면 변경 없이 취소된다.

## 12.4 Counters

```text
============================================================
 4. Counters
============================================================

  RS485 RX Total        : 128
  Forwarded            : 42
  Ignored No IP        : 86
  Broadcast RX         : 1
  Broadcast Forwarded  : 3
  IP TX Success        : 45
  IP TX Failed         : 0
  RS485 TX Response    : 0
  Malformed Packet     : 0
  Buffer Overflow      : 0
  Packet Timeout       : 0
  Wi-Fi Reconnect      : 0
  Uptime               : 00:12:34

------------------------------------------------------------
 Options
------------------------------------------------------------
  1. Reset Counters
  0. Back to Main Menu
  (Press Enter with no input to refresh)
```

아무 입력 없이 Enter만 누르면 화면이 최신 카운터 값으로 새로고침된다.

카운터 의미:

| Counter             | 의미                                          |
| ------------------- | --------------------------------------------- |
| RS485 RX Total      | RS485에서 받은 완성된 VISCA 패킷 수           |
| Forwarded           | IP가 설정된 카메라라서 IP로 전달한 패킷 수    |
| Ignored No IP       | 해당 카메라 번호에 IP가 없어서 무시한 패킷 수 |
| Broadcast RX        | `0x88` Broadcast 명령을 받은 횟수             |
| Broadcast Forwarded | Broadcast로 인해 실제 IP 전송된 총 횟수       |
| IP TX Success       | IP 전송 성공 횟수                             |
| IP TX Failed        | IP 전송 실패 횟수                             |
| RS485 TX Response   | RS485 컨트롤러로 응답을 보낸 횟수             |
| Malformed Packet    | 형식 이상 패킷 수                             |
| Buffer Overflow     | 수신 버퍼 초과 횟수                           |
| Packet Timeout      | 패킷 완성 전 timeout 횟수                     |
| Wi-Fi Reconnect     | Wi-Fi 재접속 횟수                             |

주의: Broadcast 하나가 여러 카메라로 전송될 수 있으므로 `Broadcast RX`와 `Broadcast Forwarded`는 다를 수 있다.

## 12.5 Debug Mode

```text
============================================================
 5. Debug Mode
============================================================

  Current Debug Mode : OFF

------------------------------------------------------------
 Options
------------------------------------------------------------
  1. Debug ON
  2. Debug OFF
  3. Show Last 20 Packets
  4. Live Packet Monitor
  0. Back to Main Menu
```

Debug Mode ON일 때는 RS485 수신과 IP 전송을 실시간으로 출력한다.

5번 카메라 명령 예:

```text
[RX] 85 01 06 04 FF
[ROUTE] CAM5 -> 192.168.1.105:5678
[REWRITE] 0x85 -> 0x81
[TX] UDP 192.168.1.105:5678 | 81 01 06 04 FF
```

### 12.5.1 Live Packet Monitor

"4. Live Packet Monitor"를 선택하면 Debug Mode가 꺼져 있어도 자동으로 켜지고(flash에 저장됨),
아래와 같은 화면으로 전환되어 RS485 <-> IP VISCA 트래픽이 실시간으로 계속 출력된다.
아무 입력 없이 Enter만 누르면 스트리밍이 멈추고 이전 메뉴(5. Debug Mode)로 돌아간다.

```text
============================================================
 5.4 Live Packet Monitor
============================================================
Streaming RS485 <-> IP VISCA traffic below.
Press Enter (no input) to return to Debug Mode menu.
------------------------------------------------------------
[RX] 85 01 06 04 FF
[ROUTE] CAM5 -> 192.168.1.105:5678
[REWRITE] 0x85 -> 0x81
[TX] UDP 192.168.1.105:5678 | 81 01 06 04 FF
```

IP가 없는 카메라 명령 예:

```text
[RX] 82 01 06 04 FF
[ROUTE] CAM2 -> No IP configured
[ACTION] Ignored
```

Broadcast 명령 예:

```text
[RX] 88 01 00 01 FF
[ROUTE] Broadcast -> all configured cameras
[TX] UDP 192.168.1.102:5678 | 81 01 00 01 FF
[TX] UDP 192.168.1.105:5678 | 81 01 00 01 FF
[TX] UDP 192.168.1.107:5678 | 81 01 00 01 FF
```

오류 패킷 예:

```text
[RX] 12 34 FF
[ERROR] Malformed packet
[ACTION] Dropped
```

---

## 13. 구현 제외 항목

다음 기능은 구현하지 않는다.

| 제외 항목      | 이유                                        |
| -------------- | ------------------------------------------- |
| ONVIF 제어     | 구현 복잡도가 높고 현재 목적은 VISCA 변환임 |
| 영상 스트리밍  | 본 장치는 제어 신호 변환 장치임             |
| RTSP 처리      | 제어 기능과 무관                            |
| Pelco-D/P 변환 | 현재 목표는 VISCA 변환                      |
| 웹 설정 UI     | 현재는 USB Serial 메뉴만 사용한다고 가정    |
| 자동 AP 모드   | 현재 핵심 요구사항 아님                     |
| NVR/VMS 연동   | 범위 밖                                     |

---

## 14. 권장 소스 구조

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
  RoutingTable.h
  RoutingTable.cpp
  SerialMenu.h
  SerialMenu.cpp
  Diagnostics.h
  Diagnostics.cpp
  Storage.h
  Storage.cpp
```

모듈별 역할:

| 모듈            | 역할                                          |
| --------------- | --------------------------------------------- |
| ViscaParser     | `0xFF` 기준 VISCA 패킷 파싱                   |
| Rs485Port       | UART2 및 DE/RE 제어                           |
| IpViscaClient   | Raw UDP/TCP IP VISCA 전송                     |
| SonyViscaClient | Sony VISCA over IP framing 및 전송            |
| RoutingTable    | 카메라 1~7 IP/Port/Protocol/Address Mode 관리 |
| SerialMenu      | USB Serial 메뉴 입력/출력                     |
| Diagnostics     | 카운터, 최근 패킷 로그, 디버그 출력 관리      |
| Storage         | Preferences/NVS 저장 및 로드                  |

---

## 15. Claude Code 구현 지시사항

이 프로젝트는 Arduino ESP32 펌웨어로 구현한다.

핵심 요구사항:

1. UART0는 USB Serial 메뉴와 디버그 전용으로 사용한다.
2. UART2를 사용하여 RS485 VISCA 패킷을 수신한다.
3. RS485 방향 제어 핀은 기본 GPIO4로 한다.
4. RS485 기본 모드는 Receive이다.
5. VISCA 패킷은 `0xFF`를 기준으로 구분한다.
6. 패킷 첫 바이트 `0x81~0x87`을 카메라 1~7로 해석한다.
7. 카메라 1~7 각각에 대해 IP, Port, Protocol, Address Mode를 저장한다.
8. 해당 카메라 번호에 IP가 설정되어 있으면 IP VISCA로 전송한다.
9. 해당 카메라 번호에 IP가 없으면 무시한다.
10. `0x88` Broadcast가 들어오면 IP가 설정된 모든 카메라에 전송한다.
11. 기본 Address Mode는 `rewrite_0x81`이다.
12. 기본 Protocol은 `IP_VISCA_RAW_UDP`이다.
13. 기본 Port는 5678이다.
14. Wi-Fi 연결 실패 시 자동 AP 모드로 전환하지 않는다.
15. Wi-Fi 연결 실패 시 Serial 메뉴는 계속 사용할 수 있어야 한다.
16. Wi-Fi 연결 실패 시 주기적으로 재접속을 시도한다.
17. 웹 설정 UI는 구현하지 않는다.
18. ONVIF는 구현하지 않는다.
19. Pelco-D/P 변환은 구현하지 않는다.
20. Debug Mode에서는 RS485 수신과 IP 전송을 실시간으로 출력한다.
21. 설정은 Preferences/NVS에 저장하고 재부팅 후 복원한다.
22. Serial 시작 메뉴는 다음 5개 항목만 사용한다.

```text
1. Network Settings
2. RS485 Settings
3. Routing Table
4. Counters
5. Debug Mode
```

가능하면 메뉴 처리는 non-blocking에 가깝게 구현한다. Serial 메뉴가 표시되어 있어도 RS485 패킷 수신과 IP 전송이 중단되지 않아야 한다.