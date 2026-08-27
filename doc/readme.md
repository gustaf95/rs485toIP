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

## 3.1 지원 보드와 UART 사용

두 보드를 지원한다. **기본 타겟은 ESP32-C3 Super Mini**이고, ESP32 클래식은 이미 설치된
장비를 위해 남겨둔다.

```bash
pio run                  # ESP32-C3 Super Mini (기본)
pio run -e esp32dev      # ESP32 클래식
```

빌드 산출물은 `.pio/`가 아니라 `C:/pio-build/<프로젝트해시>`에 쌓인다. **이 프로젝트 경로에 한글이 섞여
있기 때문이다** — ESP32-C3용 RISC-V 링커가 non-ASCII 경로를 깨뜨려
`ld.exe: cannot open map file ...`로 링크에 실패한다. 클래식용 Xtensa 툴체인은 같은
경로에서 멀쩡히 링크되므로, 이 설정이 없으면 "클래식은 되는데 C3만 안 되는" 상태가 되어
원인을 엉뚱한 데서 찾게 된다. 자세한 것은 `platformio.ini`의 `build_dir` 주석에 있다.

**VS Code에서 빨간 줄이 뜨거나 디버그가 안 잡히면** `.vscode/c_cpp_properties.json`과
`launch.json`이 낡은 것이다 — 둘 다 PlatformIO가 자동 생성하는 파일인데, `build_dir`이
바뀌거나 디렉터리가 없어지면 지워진 경로를 그대로 가리킨 채 남는다(빌드 자체는 멀쩡한데
IDE만 에러를 내므로 헷갈리기 쉽다). 다음으로 다시 만든다:

```bash
pio project init --ide vscode -e esp32c3_supermini
```

보드마다 다른 사실(어느 UART를 쓰는지, 기본 GPIO, 예약 핀 목록)은 전부
[BoardProfile.h](src/BoardProfile.h) 한 곳에 있다. **다른 파일에 GPIO 번호나 UART 번호를
직접 적으면 안 된다** — 보드를 늘릴 때 반드시 한 곳을 빠뜨린다.

### ESP32-C3 Super Mini (기본)

| 용도                   | 인터페이스            | 기본 핀                   | 설명                          |
| ---------------------- | --------------------- | ------------------------- | ----------------------------- |
| USB Serial 디버그/설정 | 네이티브 USB (CDC)    | USB-C 커넥터              | 보드레이트 개념 없음          |
| RS485 VISCA 수신/송신  | UART1 / Serial1       | RX = GPIO4, TX = GPIO5    | RS485 모듈 연결               |
| RS485 방향 제어        | GPIO                  | GPIO6                     | DE, /RE 제어                  |
| 상태 LED               | GPIO                  | GPIO8 (온보드, 액티브 로우) | 3.4절                        |

- **C3의 UART는 0번과 1번 두 개뿐이다.** 클래식의 UART2를 그대로 쓰면 컴파일도 되고
  부팅도 되는데 RS485만 조용히 죽는다 — arduino-esp32의 `HardwareSerial::begin()`이
  없는 번호를 받으면 `log_e()` 한 줄 남기고 그냥 `return`하기 때문이다(기본 로그 레벨에서는
  그마저 안 보인다). 그래서 [Rs485Port.cpp](src/Rs485Port.cpp)에 `static_assert`로 못 박아
  뒀다 — 새 보드를 추가하면 런타임이 아니라 빌드에서 먼저 걸린다.
- **USB 콘솔이 UART가 아니다.** Super Mini의 USB-C는 CH340 같은 브리지 칩이 아니라 칩에
  내장된 USB Serial/JTAG다. `platformio.ini`의 `-DARDUINO_USB_CDC_ON_BOOT=1`이 없으면
  `Serial`이 UART0(GPIO20/21)로 가버려서 USB를 꽂아도 메뉴가 나오지 않는다.
  - 보드레이트는 무의미해진다. `monitor_speed`도 `SERIAL_CONSOLE_BAUD`도 무시되고 속도는
    USB가 정한다. 아래 클래식 항목의 "9600bps면 loop()가 177ms 멈춘다" 문제가 그래서
    C3에서는 대체로 사라진다.
  - 대신 다른 구멍이 하나 있다. 터미널이 **열려 있는데 읽어가지 않으면** `Serial.write()`가
    링버퍼가 빌 때까지 최대 100ms(기본값) `loop()`를 붙잡는다. C3는 싱글코어라 그동안
    RS485 처리도 같이 멈추므로, `setup()`에서 `Serial.setTxTimeoutMs(20)`으로 상한을
    낮춰뒀다. 호스트가 아예 안 붙어 있으면 arduino-esp32가 출력을 조용히 버리므로
    블로킹이 없다.
- **GPIO20/21에 RS485를 물리지 않는다.** ROM 부트로더가 USB CDC와 무관하게 GPIO21로
  115200bps 부팅 로그를 뿜는다. 이미 ZU-EPC7000이 마스터로 도는 버스에 리셋할 때마다
  쓰레기 바이트가 실린다. 거부하지는 않고 설정 화면에서 경고한다(12.2절).
- **핀이 13개뿐이다.** Super Mini에서 실제로 뽑혀 나오는 것은 GPIO0~10, 20, 21이다.
  11~17은 패키지 내장 SPI 플래시가, 18/19는 USB D-/D+가 쓴다.

### ESP32 클래식 (esp32dev)

| 용도                   | ESP32 인터페이스 | 기본 핀                    | 설명                              |
| ---------------------- | ---------------- | -------------------------- | --------------------------------- |
| USB Serial 디버그/설정 | UART0 / Serial   | TX0, RX0                   | PC와 연결, 메뉴 출력 및 설정 입력 (**115200bps**) |
| RS485 VISCA 수신/송신  | UART2 / Serial2  | RX2 = GPIO25, TX2 = GPIO26 | RS485 모듈 연결                   |
| RS485 방향 제어        | GPIO             | GPIO27                     | DE, /RE 제어                      |
| 상태 LED               | GPIO             | GPIO13 (외부, 액티브 하이) | 3.4절                             |

- UART0는 USB Serial 전용으로 사용한다. 속도는 RS485와 무관하게 **115200bps** 고정이다
  (`SERIAL_CONSOLE_BAUD`, `platformio.ini`의 `monitor_speed`와 같아야 함).
  - 9600bps로 두면 Debug Mode에서 문제가 생긴다. 패킷당 로그가 170자에 가까운데 9600bps로는
    그것만 약 177ms가 걸리고, `Serial.print()`는 TX 버퍼가 차면 블로킹하므로 그동안 `loop()`가
    멈춰 RS485 수신 바이트를 놓친다. 유실된 프레임에 Stop 명령이 섞여 있으면 카메라가 멈추지
    않는다. 115200이면 같은 출력이 약 15ms로 줄어든다.
- RS485를 TX0/RX0에 연결하지 않는다. 공유하면 업로드, 디버그, RS485 통신이 충돌한다.

### 두 보드 공통

- RS485 수신 링버퍼를 기본값 256에서 `RS485_RX_BUFFER_SIZE`(1024)로 키워 `loop()`가 잠시
  멈춰도 바이트가 넘치지 않게 했다 (`Rs485Port::begin()`에서 `setRxBufferSize()`를
  `begin()` 앞에 호출). 9600bps 기준 약 1초의 정체를 견딘다 — **그보다 오래 `loop()`를
  붙잡는 코드를 새로 넣으면 프레임이 유실된다.** 웹 설정 화면의 Wi-Fi 스캔을 비동기로
  바꾼 것이 그 때문이다(12.1절).

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

| RS485 모듈 핀 | ESP32-C3 Super Mini | ESP32 클래식             |
| ------------- | ------------------- | ------------------------ |
| VCC           | 3V3                 | 3V3                      |
| GND           | GND                 | GND                      |
| RO            | GPIO4               | GPIO25 / RX2             |
| DI            | GPIO5               | GPIO26 / TX2             |
| DE            | GPIO6               | GPIO27                   |
| /RE           | GPIO6               | GPIO27                   |
| A 또는 +      | PTZ 컨트롤러 RS485 A / + | PTZ 컨트롤러 RS485 A / + |
| B 또는 -      | PTZ 컨트롤러 RS485 B / - | PTZ 컨트롤러 RS485 B / - |

DE/RE 핀에 **외부 풀다운 저항(10k 정도)**을 다는 것을 권한다. 부팅이 끝나
`Rs485Port::begin()`이 `pinMode(OUTPUT)`을 걸기 전까지 이 핀은 떠 있는데, 그 사이에
트랜시버가 송신 모드로 들어가면 버스를 물어버린다. 이미 ZU-EPC7000이 마스터로 도는
버스라 그 몇 밀리초가 실제로 문제가 된다.

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

| 항목      | ESP32-C3 Super Mini            | ESP32 클래식        |
| --------- | ------------------------------ | ------------------- |
| 기본 GPIO | GPIO8 (보드 내장 파란 LED)     | GPIO13 (외부 LED)   |
| 극성      | 액티브 로우 (LOW = 켜짐)       | 액티브 하이 (HIGH = 켜짐) |
| 용도      | Wi-Fi 연결 상태 / RS485 신호 수신 표시 | 좌동          |

핀과 **극성 둘 다** 런타임 설정이다 — RS485 Settings 화면(Serial의 `a`번, Web의
"Status LED Logic")에서 바꿀 수 있다. 어느 쪽이 맞는지는 보드가 아니라 배선이 정하기
때문이다: 보드 내장 LED는 보통 `3V3 → LED → GPIO`라 액티브 로우고, 외부 LED를
`GPIO → LED → GND`로 달면 액티브 하이다. 극성을 반대로 두면 "안 켜진다"가 아니라
**"계속 켜져 있고 가끔 꺼진다"**로 보이므로 증상만으로는 헷갈리기 쉽다.

C3의 GPIO8은 부팅 스트래핑 핀이지만 이 배선에서는 안전하다 — LED를 통해 3V3으로
끌려 올라가는데 그게 마침 정상 부팅 조건이다.

동작 방식 (`StatusLed` 클래스, [StatusLed.h](src/StatusLed.h) / [StatusLed.cpp](src/StatusLed.cpp)):

| 상황                     | LED 동작                                  |
| ------------------------ | ------------------------------------------ |
| Wi-Fi 연결됨             | 항상 켜짐(고정 ON)                         |
| Wi-Fi 연결 안 됨         | 1초 간격으로 깜박임                        |
| RS485 VISCA 패킷 수신    | 0.2초 간격으로 2회 깜박인 뒤 위 상태로 복귀 |

RS485 패킷 수신 표시가 Wi-Fi 상태 표시보다 우선하며, 2회 깜박임이 끝나면 그 시점의 Wi-Fi 연결 여부에 따라 원래 패턴(고정 ON 또는 1초 깜박임)으로 자동 복귀한다. `delay()`를 쓰지 않는 `millis()` 기반 non-blocking 상태 머신이라 RS485 수신/IP 전송을 막지 않는다.

상태 LED에 배정된 핀은 RS485 쪽에서 다시 쓸 수 없다 — Serial/Web 메뉴의 RS485
RX/TX/DE-RE Pin 설정에 그 번호를 넣으면 "reserved for the status LED" 오류로 거부된다.
반대 방향도 마찬가지다.

## 3.5 ESP32-C3 Super Mini 실전 주의사항

빌드/설정이 아니라 **물건 자체**의 문제들이다. 미리 알아두지 않으면 배선이나 코드를
의심하며 시간을 날린다.

**안테나가 약하다.** Super Mini는 온보드 안테나의 임피던스 매칭 결함으로 수신 감도가
나쁘다는 보고가 널리 있다. 개체 편차도 크다. Wi-Fi 게이트웨이가 본업인 장비라 이게
제일 현실적인 위험이다 — **설치할 자리에서 먼저 실측하라.** 웹 설정 화면의 Wi-Fi 스캔
목록에 뜨는 dBm 값이 그 자리의 실제 감도다. AP가 멀면 클래식 보드나 외장 안테나가 달린
C3 모듈을 쓰는 편이 낫다.

**부팅 루프가 돌면 플래시 모드를 낮춘다.** Super Mini의 ESP32-C3FH4는 패키지 내장
플래시인데, `qio`에서 부팅이 안 되는 개체가 있다. `platformio.ini`의 해당 env에
`board_build.flash_mode = dio`를 추가하면 된다.

**업로드가 안 잡히면 수동으로 부트 모드에 넣는다.** BOOT 버튼(GPIO9)을 누른 채 USB를
꽂거나 RST를 눌렀다 떼면 다운로드 모드로 들어간다. 네이티브 USB라 펌웨어가 USB를
초기화하기 전에 죽으면 포트 자체가 사라지는데, 그때도 이 방법으로 복구된다.

**핀이 13개다.** GPIO0~10, 20, 21만 뽑혀 나온다. 이 게이트웨이가 쓰는 것은 RS485 세 개
(RX/TX/DE-RE)와 상태 LED 하나인데, LED는 온보드(GPIO8)를 쓰므로 실제로 배선할 핀은
세 개뿐이다. 12.2.2절의 경고 대상(2/8/9 스트래핑, 20/21 부팅 로그)을 피하면
0, 1, 3, 4, 5, 6, 7, 10이 남는다 — 기본값 4/5/6이 그중 하나다.

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

IP 카메라로 보낼 때 첫 바이트를 어떻게 처리할지는 슬롯별 Address Mode로 정한다(5.2절). IP 주소로 이미 대상 카메라가 정해지므로, 카메라가 자기 VISCA 주소를 1번으로 쓰고 있다면 `rewrite_0x81`이 가장 안전하다.

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
| Address Mode  | IP 전송 시 첫 바이트 처리 | preserve         |
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

Broadcast도 슬롯별 Address Mode 설정을 그대로 적용한다.

## 5.2 Address Mode

| Address Mode   | 설명                                               |
| -------------- | -------------------------------------------------- |
| rewrite_0x81   | IP 카메라로 보낼 때 첫 바이트를 항상 `0x81`로 변경 |
| preserve       | RS485에서 받은 첫 바이트를 그대로 유지             |
| rewrite_by_cam | 카메라 번호에 맞춰 `0x81~0x87`로 변경              |

기본값은 `preserve`이다 - Pelco-D 입력 경로에서는 게이트웨이가 카메라 번호로 만들어낸 `0x81~0x87`이 그대로 나간다. 카메라가 자기 VISCA 주소를 1번으로 쓰고 있다면 슬롯별로 `rewrite_0x81`로 바꿔야 한다.

---

## 6. RS485 VISCA 패킷 처리

펌웨어는 다음 방식으로 VISCA 패킷을 처리한다.

1. RS485 UART(3.1절)에서 바이트를 계속 읽는다.
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
| AP SSID               | Web Config Server AP 이름 | `RS485Gateway-XXXX` (XXXX=MAC 뒷자리 4자리)   |
| AP Password           | Web Config Server AP 비밀번호 | `config.h`의 `AP_PASSWORD_DEFAULT`        |
| RS485 Baudrate        | RS485 속도               | 9600                                           |
| RS485 RX Pin          | RS485 UART RX            | C3: GPIO4 / 클래식: GPIO25                     |
| RS485 TX Pin          | RS485 UART TX            | C3: GPIO5 / 클래식: GPIO26                     |
| RS485 DE/RE Pin       | 방향 제어 핀             | C3: GPIO6 / 클래식: GPIO27                     |
| RS485 Signal Inversion | UART 신호 극성 반전     | off (정상 결선 가정, A/B 반전 배선이면 켜야 함) |
| Status LED Pin        | 상태 LED 핀              | C3: GPIO8 / 클래식: GPIO13                     |
| Status LED Polarity   | 액티브 로우 여부         | C3: Active Low / 클래식: Active High           |
| Input Protocol        | RS485 입력 프로토콜      | Pelco-D                                        |
| Pelco Response Mode   | Pelco ACK 합성 여부      | none (응답 안 함)                              |
| Camera 1 IP           | CAM1 IP                  | empty                                          |
| Camera 1 Port         | CAM1 Port                | 5678                                           |
| Camera 1 Protocol     | CAM1 Protocol            | IP_VISCA_RAW_UDP                               |
| Camera 1 Address Mode | CAM1 Address Mode        | preserve                                       |
| Camera 1 Auto Power Control | CAM1 전원 자동 제어 | off                                            |
| Camera 2~7 설정       | 위와 동일                | empty / 5678 / IP_VISCA_RAW_UDP / preserve / off |
| Response Mode         | 카메라 응답 처리 모드    | none                                           |
| Debug Mode            | 디버그 출력 여부         | off                                            |

### 10.0.1 설정을 추가해도 기존 설정이 날아가지 않는다

예전에는 `Storage::load()`가 `저장된 크기 == sizeof(SystemConfig) && version == 최신`을
요구했다. 그래서 설정 항목을 **하나만 추가해도** 현장 장비의 Wi-Fi 비밀번호, 카메라 IP,
RS485 핀이 전부 기본값으로 돌아갔다. "안 쓰는 필드라도 구조체에서 빼지 말 것"이라는
제약(10.1절)도 여기서 나온 것이다.

지금은 저장된 블롭이 현재 구조체보다 **짧아도** 받아준다:

1. 호출자가 `applyDefaults()`로 기본값을 채운 상태에서 `load()`를 부른다.
2. `load()`는 그 기본값 **위에** 저장된 바이트를 덮어쓴다.
3. 옛 블롭에 없던 뒷부분은 자연히 기본값으로 남는다.
4. 옛 버전을 읽었으면 `upgradedOut`으로 알려주고, `setup()`이 그때 한 번만 다시 저장해
   flash를 최신 레이아웃으로 올린다.

**그래서 새 필드는 반드시 `SystemConfig`의 맨 끝, `cameras[]` 뒤에 붙여야 한다.** 중간에
끼워 넣으면 옛 펌웨어가 저장한 바이트가 한 칸씩 밀려 엉뚱한 필드로 읽히는데, 그건 설정이
초기화되는 것보다 훨씬 나쁘다(잘못된 핀으로 조용히 동작한다). `Storage.cpp`의
`static_assert`가 이걸 빌드 타임에 막는다.

호환을 보장하는 범위는 **버전 10 이상**이다. 그 이전은 `CameraSlot` 중간에 필드가 들어간
적이 있어(`autoPowerControl`) 접두사 관계가 성립하지 않으므로 거부하고 기본값으로 간다.
새 펌웨어가 쓴 더 큰 블롭을 옛 펌웨어가 읽는 경우(다운그레이드)도 `Preferences::getBytes()`가
0을 반환해 조용히 깨지지 않고 안전하게 기본값으로 떨어진다.

## 10.1 삭제된 기능의 잔여 값 처리

**Raw Bridge**(Input Protocol의 다섯 번째 선택지)와 **UART0 Shared Mode**(RS485를 USB
콘솔과 같은 UART에 물리던 모드)는 실제 운용에 쓰이지 않아 제거했다. 다만 **`SystemConfig`
구조체에서는 해당 필드를 빼지 않았다** — 필드를 빼면 뒤따르는 모든 필드의 위치가 밀려서
옛 블롭이 엉뚱하게 읽힌다(10.0.1절). `rs485Uart0Shared` 필드는 자리만 남아 있고 아무도
읽지 않는다.

같은 이유로 **삭제된 기능이 쓰던 열거형 값도 재사용하지 않는다** — `ProtocolMode`의 `3`
(`RAW_DATA_UDP`)과 `InputProtocol`의 `4`(`RAW_BRIDGE`)는 비워둔 채로 둔다. 새 항목에 그
번호를 주면 옛 설정이 엉뚱한 기능으로 되살아난다.

이전 펌웨어에서 그 값들을 저장해둔 기기를 위해, 부팅 시 `RoutingTable::sanitizeRemovedFeatures()`가
한 번 걸러낸다. 고칠 게 있었을 때만 flash에 다시 쓴다.

| 저장된 값 | 되돌리는 값 | 이유 |
| --- | --- | --- |
| Input Protocol = Raw Bridge | Pelco-D | 그 입력을 해석할 코드가 없다. 기본값으로 되돌리는 게 안전하다 |
| Camera Protocol = RAW_DATA_UDP | IP_VISCA_RAW_UDP + **IP 지움** | 그 슬롯의 IP는 카메라가 아니라 상대 게이트웨이 주소다. 그대로 두면 그쪽으로 VISCA를 쏘게 된다 |
| UART0 Shared Mode = on | off + 그 보드의 RS485 핀 기본값 | 그 모드가 강제해둔 GPIO3/1은 UART0 핀이라 메뉴로도 못 고친다(`validateRs485Pin()`이 거부) |

---

## 11. Serial 시작 메뉴

전원 투입/리셋 직후에는 메뉴가 자동으로 뜨지 않을 뿐 아니라, **Serial에 어떤 메시지도
찍히지 않는다** — 부팅 배너, Wi-Fi STA 연결 시도/성공/실패 메시지, 재접속 메시지까지
전부 메뉴가 잠금 해제되기 전까지는 완전히 침묵한다. 게이트웨이는 메뉴 조작 없이도
RS485↔IP 변환을 계속 수행하므로, 아무도 콘솔을 보고 있지 않을 때(무인 설치 환경 등)
어떤 텍스트도 끼어들지 않게 하기 위함이다. 안내 메시지조차 출력하지 않으므로, "Enter를
두 번 누르면 메뉴가 열린다"는 사실은 이 문서로만 알 수 있다.

아무것도 입력하지 않고 **Enter를 연속 두 번** 누르면 그때 Main Menu가 열리고, 그 순간부터
이후의 Serial 출력(Wi-Fi 상태 메시지 등)도 정상적으로 찍히기 시작한다. 중간에 뭔가 입력하고
Enter를 치면 카운트가 리셋된다(로그성 노이즈 등으로 우연히 열리는 걸 방지). 한 번 열리면
이후에는 평소처럼 번호를 입력해 메뉴를 탐색하면 되고, 다시 잠그려면 리셋해야 한다.

주의: Debug Mode에서 나오는 RX/TX 로그(`[RX]`, `[TX]` 등)는 이 잠금과 무관하다 — Debug
Mode는 Serial이든 Web이든 사용자가 명시적으로 켠 기능이라, 메뉴가 잠겨 있어도 Debug Mode가
ON이면 정상적으로 출력된다.

메뉴가 열리면 다음과 같이 출력된다.

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
  6. Factory Reset

Select menu number:
============================================================
```

한글 Serial 출력은 환경에 따라 깨질 수 있으므로 기본 메뉴는 영문으로 출력한다.

"6. Factory Reset"은 Wi-Fi/RS485/Routing Table/Input Protocol 등 저장된 설정을 전부 지우고
소스 코드의 기본값(`config.h`)으로 되돌린 뒤 자동으로 재부팅한다. 되돌릴 수 없는 동작이라
확인 절차를 거친다:

```text
WARNING: This erases ALL settings (Wi-Fi, RS485, routing table,
input protocol, everything) and restores factory defaults, then
reboots. This cannot be undone.
Type YES to confirm, or press Enter to cancel:
```

정확히 대문자 `YES`를 입력해야 실행되며, 그 외 입력(빈 줄 포함)은 전부 취소로 처리되고 Main
Menu로 돌아간다. 확인되면 RS485 UART를 새 기본값으로 즉시 재적용하고 flash에 저장한 뒤
`ESP.restart()`로 재부팅한다 — Wi-Fi 재연결 등 나머지 초기화는 `setup()`이 처음부터 다시
수행한다.

---

## 12. Serial 메뉴 상세

## 12.1 Network Settings

```text
============================================================
 1. Network Settings
============================================================

  Wi-Fi Mode       : AP+STA
  Wi-Fi Status     : Connected
  SSID             : Your_AP_Name
  DHCP             : Enabled
  ESP32 IP         : 192.168.1.50
  AP SSID          : RS485Gateway-3F2A
  AP IP            : 192.168.4.1
  Gateway          : 192.168.1.1
  Subnet           : 255.255.255.0

------------------------------------------------------------
 Options
------------------------------------------------------------
  1. Set Wi-Fi SSID
  2. Set Wi-Fi Password
  3. Set DHCP / Static IP
  4. Retry Wi-Fi Connection
  5. Set AP SSID
  6. Set AP Password
  0. Back to Main Menu
```

Network Settings의 값(SSID, 비밀번호, DHCP/Static IP, Gateway, Subnet, AP SSID/Password)은
별도의 저장 단계 없이 변경 즉시 flash(NVS)에 기록된다.

Wi-Fi STA 연결 실패 시에도 Serial 메뉴는 계속 사용 가능하고, ESP32는 주기적으로 STA 재접속을
시도한다. **AP는 STA 연결 여부와 무관하게 항상 켜져 있다** — `WiFi.mode(WIFI_AP_STA)`로
STA/AP를 동시에 운용한다(13절 "Web Config Server" 참고). USB Serial에 물리적으로 접근할
수 없는 환경(이미 설치돼서 USB 케이블을 다시 꽂기 번거로운 장비 등)에서도 접근 경로를
보장하기 위함이다.

AP SSID/Password는 **"5. Set AP SSID"/"6. Set AP Password"로 직접 바꿀 수 있다** — 저장 즉시
`WiFi.softAP()`를 재적용해서 재부팅 없이 반영된다. 처음 값(아직 한 번도 안 바꿨을 때)은
`RS485Gateway-XXXX`(뒤 4자리는 MAC 주소 기반, 기기별로 다름) 형태로 최초 부팅 시 한 번
만들어져 flash에 저장되고, 그 뒤로는 계속 그 값을 쓴다(직접 바꾸기 전까지). AP IP는 ESP32
기본값인 `192.168.4.1`로 고정이다. AP Password 기본값은 `config.h`의
`AP_PASSWORD_DEFAULT`이며, 두 값 모두 입력을 비워서 Enter를 치면 취소(변경 없음)로
처리된다 — 원래 자동 생성 SSID로 되돌리려면 "6. Factory Reset"이 필요하다.

Wi-Fi STA 연결 실패 시 출력:

```text
[NETWORK]
  Wi-Fi Status : Disconnected
  ESP32 IP     : Not assigned

[WARN] Wi-Fi connection failed.
[INFO] IP forwarding is unavailable until network is restored.
[INFO] Use Serial menu (or the AP) to update Wi-Fi settings.
```

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

  Board            : ESP32-C3 Super Mini
  UART Port        : UART1 / Serial1
  RX Pin           : GPIO4
  TX Pin           : GPIO5
  DE/RE Pin        : GPIO6
  Baudrate         : 9600
  Format           : 8N1
  Signal Inversion : Normal
  Default Mode     : Receive
  Input Protocol   : Pelco-D
  Pelco Response   : No response
  Camera Response  : none (drop camera responses)
  Status LED Pin   : GPIO8
  Status LED Logic : Active Low (LOW = on, e.g. C3 Super Mini onboard LED)

------------------------------------------------------------
 Options
------------------------------------------------------------
  1. Set Baudrate
  2. Set RX Pin
  3. Set TX Pin
  4. Set DE/RE Pin
  5. Set Input Protocol
  6. Set Pelco Response Mode
  7. Set Status LED Pin
  8. Set Signal Inversion
  9. Set Camera Response Mode
  a. Set Status LED Polarity
  0. Back to Main Menu
```

(위는 기본 타겟인 ESP32-C3 Super Mini의 값이다. `-e esp32dev`로 빌드하면 `Board`,
`UART Port`, 핀 기본값이 클래식 것으로 바뀐다 — 3.1절 참고.)

Baudrate/RX Pin/TX Pin/DE-RE Pin은 값을 입력하는 즉시 flash에 저장되고 UART에 재적용되며,
별도의 저장 메뉴는 없다.

"8. Set Signal Inversion"은 UART 신호의 극성을 뒤집는다. 기본값은 **Normal**이다 — 정상
결선(A/B 안 뒤집힘)을 기본 가정으로 삼는다. RS485 A/B(D+/D-)가 뒤집혀 결선된 배선이면
Inverted로 바꿔야 한다. 반전이 안 맞으면 수신 바이트가 전부 깨지는데, 증상이 특징적이다 —
0xFF로 시작해야 할 Pelco-D 프레임이 `00`으로 시작하고, 뒤 바이트들은 한 비트씩 밀린
보수값이 되어 12.5 Live Packet Monitor에 `00 BE 59 DF 45` 같은 패턴이 반복해서 찍힌다.
값을 선택하는 즉시 flash에 저장되고 UART에 재적용된다(재부팅 불필요).

ESP32 UART는 이 플래그 하나로 RXD/TXD를 함께 반전시키므로 수신뿐 아니라 합성 ACK 송신도
같은 극성으로 나간다. A/B가 뒤집힌 버스에서는 양방향 모두 이게 맞다. 정석은 하드웨어에서
A/B를 바로잡는 것이고, 이 옵션은 결선을 손댈 수 없는 현장을 위한 소프트웨어 보정이다.

```text
1. Inverted (A/B swapped wiring)
2. Normal
```

"5. Set Input Protocol"은 RS485로 들어오는 입력이 어느 프로토콜인지 선택한다. 값을 선택하는
즉시 flash에 저장된다. 기본값은 **Pelco-D**이다 — 이 프로젝트의 RS485 컨트롤러(ZU-EPC7000)와
기존 카메라(EDIS ED-P) 조합이 Pelco-D로 운용되어 왔기 때문에, 신규 설치 시 가장 먼저 맞아야 할
값을 기본값으로 삼았다.

```text
1. VISCA
2. Pelco-D
3. Pelco-P
4. Pelco-D/P Autodetect
```

Pelco-D를 선택하면 `PelcoDParser`가 0xFF로 시작하는 고정 7바이트 프레임(Address, Command1,
Command2, Data1, Data2, Checksum, 합산 체크섬)을 조립하고 체크섬을 검증한다. Pelco-P를
선택하면 `PelcoPParser`가 0xA0으로 시작해 0xAF로 끝나는 고정 8바이트 프레임(XOR 체크섬)을
조립한다. 체크섬(또는 Pelco-P의 경우 ETX 고정 바이트)이 맞지 않으면 Malformed Packet
카운터가 올라간다.

유효한 패킷은 `translatePelcoAndForward()`가 VISCA 명령으로 변환해서 카메라로 전달한다
(`RS485 RX Total` 카운터 반영, Debug Mode에서 `[PELCO-D RX]`/`[PELCO-P RX]` → `[PELCO-D->VISCA]`/
`[PELCO-P->VISCA]` → `[TX]` 순서로 로그가 찍힌다). 지원 범위는 FoMaKo 카메라 자체가 Pelco-D/P로
지원하는 명령에 맞춰져 있다.

> 아래는 요약이고, **입력 바이트 ↔ 출력 VISCA 전체 대응표와 각 항목의 근거(실측/매뉴얼/추론)는
> [pelcoD_command.md 12절](pelcoD_command.md#12-실측-확인된-명령어-셋-운용-확정)**에 있다.

- **번역됨**: Pan/Tilt 이동(대각선 포함), Stop(Pan/Tilt+Zoom+Focus 동시 정지), Zoom Tele/Wide,
  Focus Near/Far, **Iris Open/Close**(→ VISCA `CAM_Iris Up/Down` `04 0B 02`/`04 0B 03`),
  Preset Set/Goto/Clear.
  - Iris는 ZU-EPC7000의 28행 커맨드 표에도 FoMaKo 자체 Pelco-D 표에도 없어서 처음엔
    빠져 있었는데, 컨트롤러가 실제로는 `FF 06 02 00 00 00 08`을 보내는 게 확인돼
    추가했다(2026-08-08). **AE 모드가 Manual일 때만 효과가 있다** — Auto면 자동 노출이
    조리개를 도로 가져간다. 게이트웨이가 모드를 대신 바꾸지는 않는다.
- **아직 번역 안 됨**: Query Pan/Tilt/Zoom Position(VISCA 쪽 비동기 응답을 Pelco Extended
  Response로 재포장하는 로직이 필요해 후속 작업으로 남겨둠), 그리고 FoMaKo 자체가 Pelco-D/P로
  지원하지 않는 Run Group/Swing·Aux·절대좌표 Set·Focus Position Query는 애초에 구현 대상이
  아님 — 이런 패킷은 카메라로 나가지 않는다(Address가 카메라 슬롯 1~7 밖이어도 마찬가지).
  다만 **번역할 비트가 하나도 없는 Standard Command는 Unhandled Commands 로그에 남긴다** —
  Iris 건이 오래 묻혀 있던 이유가 이런 패킷을 아무 기록 없이 버렸기 때문이다.
- **EDIS 벤더 확장 (`0xC3` SET / `0xD3` GET)**: ZU-EPC7000 ↔ EDIS ED-P 사이에서 실측한
  벤더 고유 명령으로, 표준 Pelco-D 확장 옵코드 표 밖의 값이다. 바로 아래 상자 참고.

> **EDIS 벤더 확장 (실측으로 복원)**
>
> ```
> SET:   FF ADDR 00 C3 pp qq CK        →  VISCA `8x 01 04 pp qq FF` 와 대체로 1:1
>                                          (예외: AWB `36`→`35`, One Push AF `18`→`38 04`)
> 거절:  FF ADDR 00 66 00 01 CK        →  실행할 수 없는 SET에만. 성공하면 응답 없음
>
> GET:   FF ADDR 00 D3 19 E6 CK        →  Iris/AWB/Focus 모드 일괄 조회
> 응답:  FF ADDR R1 D7 19 D2 CK
>          R1 = 0x50 | (VISCA WB 모드 코드 & 0x0F)
>          D2 = (Iris가 Auto가 아니면 0x40) | (Focus가 Auto가 아니면 0x01)
>
> GET:   FF ADDR 00 D3 04 pp CK        →  단일 항목 조회, pp가 VISCA 명령 코드
> 응답:  FF ADDR 00 D7 00 <code> CK      pp=00 CAM_Power / pp=33 CAM_Back Light
> ```
>
> 단일 항목 조회는 모드 조회와 응답 배치가 다르다 — RESP1에 데이터가 실리지 않고
> CMND1(0x00)이 그대로 에코되며, 값 코드는 VISCA 그대로(`0x02`/`0x03`)다. **응답만 봐서는
> 어느 항목의 답인지 구분되지 않으므로**(전원과 Back Light 응답이 바이트까지 동일)
> 컨트롤러가 질문 순서로 짝을 맞춘다. 게이트웨이도 `DATA2`를 보고 그 자리에서 답한다 —
> `DATA1`만 보고 분기하면 BLC를 물었는데 전원 상태로 답하게 된다.
>
> **카메라가 자고 있으면 조회에 답하지 않는다 — 단 전원 조회(`D3 04 00`)만은 예외로
> 답한다.** 잠든 실물 ED-P가 전원 조회에 자기 상태를 정직하게 돌려주는 것이 실측으로
> 확인됐다(2026-08-12): `FF 03 00 D3 04 00 DA` → `FF 03 00 D7 00 03 DD`(값 `03` =
> Standby). 하필 전원 조회가 "너 켜져 있냐"는 질문 그 자체라, 침묵하면 컨트롤러가
> "자는 중"과 "그 주소에 아무것도 없음"을 구분할 수 없다. 나머지 조회는 침묵을 유지한다.
> 전원 상태는 VISCA `CAM_PowerInq`(`09 04 00`)를 주기적으로
> 던져 확인하고, 컨트롤러가 `C3 00 03`으로 스탠바이를 명령하면 그 자리에서 즉시 반영된다.
>
> 게이트웨이가 보낸 응답은 Debug Mode의 `[TX] Mode status | FF 06 55 D7 19 41 8C` 로그와
> Raw Byte Monitor의 `[TX ]` 줄에서 확인할 수 있다 (12.5.2절 참고).
>
> SET의 `pp`/`qq`가 VISCA 명령 코드/값과 대체로 같다는 것이 실측으로 확인됐다:
> Power On/Standby = `00 02`/`00 03`, Iris Auto/Manual = `39 00`/`39 03`,
> Focus Auto/Manual = `38 02`/`38 03`, Back Light On/Off = `33 02`/`33 03`,
> R/B Gain Up/Down = `03 02`/`03 03`, `04 02`/`04 03`, BRIGHT Up/Down = `0E 02`/`0E 03`,
> One Push AF = `18 01`. **성공한** SET에는 응답이 없고 컨트롤러는 주기적인 GET 폴링으로 화면을
> 갱신한다 — 실행할 수 없는 SET에는 거절 응답 `FF ADDR 00 66 00 01 CK`가 온다(2026-08-12,
> `todo.md` 2.4절). VISCA의 `y0 60 41 FF`에 해당하는 자리이며, 게이트웨이는 아직 합성하지 않는다.
> BACK LIGHT 키는 패널상 토글이지만 컨트롤러가 On/Off를
> 명시적으로 번갈아 보내므로, 게이트웨이가 토글 상태를 기억할 필요는 없다.
>
> **AWB(`36`)와 BRIGHT(`0E`)는 이름과 코드가 어긋난다** — AWB의 `pp`는 VISCA `CAM_WB`(`35`)가
> 아니고, BRIGHT 키는 `CAM_Bright`(`0D`)가 아니라 `CAM_ExpComp`(`0E`)다. `pp`를 짐작하지 말고
> 실제 바이트를 캡처해서 확인해야 하는 이유다.
>
> **예외 하나 — One Push AF**: `pp`가 VISCA 코드와 같다는 게 FoMaKo가 그 코드를
> 받아준다는 뜻은 아니다. `C3 18 01`을 `04 18 01`(One Push Trigger)로 그대로 넘겼더니
> 카메라가 `E0 60 02 FF`를 돌려줬다 — 에러 코드 `0x02`는 Syntax Error, 즉 **명령 자체를
> 모른다**는 뜻이다(실측 2026-08-08). FoMaKo 매뉴얼의 `CAM_Focus` 표에도 `04 08`/`04 48`/
> `04 38`만 있고 `04 18` 행이 없다 — 반면 `CAM_WB`에는 One Push Trigger(`04 10 05`)가
> 명시돼 있어, Focus에만 트리거를 안 넣은 게 확인된다. 그래서 게이트웨이는 이것만
> `04 38 04`(Focus AF 모드 = One Push)로 바꿔 보낸다. 이쪽은 `E0 41 FF`/`E0 51 FF`
> (ACK/Completion)를 받고 실제로 초점을 잡는다. 컨트롤러 LCD 표시는 그대로 Manual인데, 아래 "알려진 한계"의
> 접어서 보고하는 규칙이 `0x04`도 Manual로 처리하기 때문이다.
>
> 그리고 **2초 뒤(`VISCA_ONE_PUSH_AF_SETTLE_MS`) `04 38 03`으로 Manual에 되돌려 놓는다**
> (`pollOnePushAfRestore()`). One Push 모드로 두면 카메라가 이후 Focus Near/Far를 거부해서
> 수동 초점 조절이 막힌다. 컨트롤러의 ONE PUSH AF 키는 Manual 상태에서 누르는 일회성
> 트리거이므로 되돌리는 게 조작 모델과도 맞고, 덕분에 연속으로 눌러도 매번 Manual →
> One Push의 실제 모드 전환이 되어 동작한다. 이 2초 안에 컨트롤러가 Focus Auto/Manual을
> 직접 지정하면 되돌리기 예약은 취소된다.
>
> **알려진 한계 — Focus One Push**: `D3 19` 응답의 Focus 필드가 1비트(Manual/Auto)뿐이라
> 카메라 OSD의 세 번째 모드(One Push)를 표현할 수 없다. 게이트웨이는 One Push(VISCA
> `0x04`)를 Manual로 접어서 보고한다.
>
> 이 확장의 전체 대응표(SET 파라미터 / GET 조회 항목 / 각 항목의 근거)는
> [pelcoD_command.md 12.3절](pelcoD_command.md#123-edis-벤더-확장-0xc3-set--0xd3-get)에 있고,
> 아직 해독하지 못한 조회 항목(`D3 16`)과 미검증 항목은 [todo.md](todo.md)에 정리했다.
>
> 응답 규격은 ED-P 4대(설정이 서로 다른)의 실측값 6건을 모두 재현한다:
>
> | Iris/AWB/Focus | R1 | D2 |
> | --- | --- | --- |
> | Manual/Manual/Manual | `55` | `41` |
> | Auto/Manual/Manual | `55` | `01` |
> | Manual/Manual/Auto | `55` | `40` |
> | Manual/Auto/Auto | `50` | `40` |
> | Auto/Auto/Auto | `50` | `00` |
>
> `R1`의 상위 니블은 실측 4대 모두 `0x5`로 고정이며 컨트롤러가 표시하지 않는 항목이라,
> 정체를 모르는 채로 관측값을 상수로 채운다(`PELCO_EDIS_STATUS_RESP1_BASE`).
> GET의 `DATA1`이 `0x19` 외의 값(`0x04`, `0x16` 관측됨)인 경우는 아직 해독하지 못했다 —
> 답을 지어내면 컨트롤러가 잘못된 값을 표시하므로 침묵한다.
>
> **게이트웨이 동작**: `C3`는 `86 01 04 pp qq FF`로 변환해 전달하되, 실측으로 확인된
> `pp`(`00`/`03`/`04`/`0E`/`33`/`36`/`38`/`39`/`18`)만 통과시킨다. 이 중 **AWB(`36`)는 VISCA 코드가 아니라
> `35`로 갈아끼워 내보내고**, One Push AF(`18`)는 `38 04`로 바꿔 보낸다. R/B Gain(`03`/`04`)과
> BRIGHT(`0E`)는 절대 모드가 아닌 상대 조정(`02` Up / `03` Down)이라 캐시하지 않는다. `D3 19`는 `modeCache`에서 응답을 조립해
> 회신한다. 캐시는 부팅 시 전부 Auto로 시작해서, `C3`가 지나갈 때 방금 설정한 값으로
> 즉시 갱신되고(낙관적), `pollCameraModeInquiries()`가 1초 주기로 VISCA 조회
> (`09 04 00`/`09 04 39`/`09 04 35`/`09 04 38`)를 하나씩 던져 실제 값으로 수렴한다. 네 조회의
> 응답이 전부 `y0 50 pp FF`로 똑같이 생겨 구분이 안 되므로, 답을 받거나 타임아웃될 때까지
> 다음 조회를 보내지 않는다.
>
> `C3` SET이 지나가면 **그 항목을 라운드로빈 순서보다 먼저**(`MODE_INQUIRY_SET_VERIFY_DELAY_MS`
> 후) 되물어 실제로 적용됐는지 확인한다. 카메라가 명령을 거부하면 캐시의 낙관적 값이 곧바로
> 실제 값으로 되돌아가므로, 컨트롤러 화면이 한 바퀴(약 4초) 동안 거짓말하지 않는다.
>
> `D3` 응답은 **IP가 설정된 슬롯에만** 나간다. IP가 비어 있는 주소는 같은 버스의 실물
> ED-P 카메라 몫이고 그쪽이 이미 스스로 답하므로, 끼어들면 버스 충돌이 난다. 이 검사가
> 충돌을 막아주므로 `pelcoResponseMode`(합성 ACK 설정)와는 독립적으로 동작한다 — 그건
> "명령을 받았다"는 ACK를 지어낼지에 대한 설정이고, 이쪽은 컨트롤러가 명시적으로 값을
> 물어본 데 대한 데이터 응답이라 성격이 다르다.
- **모르는 Extended Command는 무시**: CMND2 bit0이 1인데 위 옵코드 중 어느 것도 아니면
  모션 비트 디코드로 넘기지 않고 버린다(Debug Mode에서 `Unknown extended command CMND2=0x..`
  로그). Standard Command는 CMND2 bit0이 항상 0으로 정의되어 있어(pelcoD_command.md 4절)
  이 구분이 안전하다.

  이 가드가 없으면 옵코드 값이 통째로 Pan/Tilt/Zoom/Focus 비트로 오독된다. 실제로
  ZU-EPC7000이 아이들 상태에서도 계속 보내는 폴링 명령(`FF ADDR 00 D3 D1 D2 CK`)의
  `CMND2=0xD3`이 Pan Right(0x02) + Tilt Down(0x10) + Zoom Wide(0x40) + Focus Far(0x80)로
  해석되어, 컨트롤러를 건드리지 않아도 카메라가 오른쪽 아래로 계속 밀리는 문제가 있었다.
  `0xD3`은 표준 Extended 옵코드 표(0x03~0x6F) 밖의 EDIS ED-P 벤더 고유 명령으로 보인다.

**중복 명령 억제**: 번역 결과가 직전에 보낸 것과 바이트 단위로 완전히 같으면,
`VISCA_DUPLICATE_SUPPRESS_MS`(200ms) 안에 들어온 재전송은 카메라로 내보내지 않는다
(`isDuplicateViscaCommand()`, Debug Mode에서 `Duplicate command suppressed` 로그).

Pelco 컨트롤러는 조이스틱을 물고 있는 동안 같은 프레임을 초당 수십 번 재전송한다. 반면
VISCA `Pan-tiltDrive`는 Stop이 올 때까지 유지되는 **래치 명령**이라 한 번만 보내면 된다.
그 재전송을 그대로 UDP로 흘리면 카메라 명령 큐가 밀려서, 조이스틱을 놓아도 밀린 명령이
다 소화될 때까지 카메라가 계속 흘러가고 반응도 굼떠진다. 방향이나 속도가 조금이라도
바뀌면 바이트가 달라져 즉시 통과하므로, Stop을 포함해 새 명령이 지연되는 일은 없다.

Pan/Tilt 속도(DATA1/DATA2)는 Pelco-D 표준 관례인 0x00~0x3F 범위를 VISCA 속도로 선형
환산한다(`scalePelcoSpeedToVisca()`, 결과는 1~viscaMax로 클램프된다). FoMaKo 매뉴얼 5.2절
기준 VISCA 상한은 팬 `0x18`, 틸트 `0x14`다. ZU-EPC7000이 조이스틱 최대 변위에서 팬/틸트
양쪽 모두 `0x3F`를 보내는 것이 실측으로 확인됐으므로(2026-08-08) 이 범위 가정이 그대로
맞고, 축별로 다른 최대치를 쓸 필요도 없다. 커맨드 세부 사항과 번역 근거는
[pelcoD_command.md](pelcoD_command.md) / [pelcoP_command.md](pelcoP_command.md) 참고.

"Pelco-D/P Autodetect"는 패킷 단위로 시작 바이트(Pelco-D는 0xFF, Pelco-P는 0xA0)를 보고
어느 프로토콜인지 실시간으로 판별한다. ZU-EPC7000 컨트롤러가 카메라 채널마다 Pelco-D/
Pelco-P를 개별 지정할 수 있어(`pelcoD_command.md` 9.1/9.3절), 같은 RS485 버스에 두
프로토콜이 실제로 섞여 들어올 가능성에 대비한 옵션이다. VISCA는 종료 바이트 0xFF가
Pelco-D의 시작 바이트와 겹쳐 안전하게 자동 판별할 수 없으므로 이 옵션에 포함되지 않는다 —
VISCA를 쓰려면 "1. VISCA"를 명시적으로 선택해야 한다.

"6. Set Pelco Response Mode"는 Pelco-D/Pelco-P/Autodetect 공통 설정이다. 유효한 Pelco
패킷을 받을 때마다 General Response(ACK, Pelco-D는 `FF ADDR 00 CKSM` 4바이트, Pelco-P는
`A0 ADDR 00 AF CKSM` 5바이트)를 RS485로 합성해서 돌려줄지 선택한다. 기본값은 **No response**
(응답 안 함)이다.

기본값을 바꾼 이유는 실측 때문이다. 이 게이트웨이가 놓이는 RS485 버스에는 컨트롤러가
직접 제어하는 실물 카메라(EDIS ED-P 등)가 같이 물려 있고, 그 카메라들은 자기 명령에
스스로 응답한다. RS485는 2선 멀티드롭이라 게이트웨이가 주소를 가리지 않고 ACK를 쏘면
실물 카메라의 응답과 **같은 버스에서 드라이버 두 개가 동시에 물려** 컨트롤러가 양쪽 다
못 읽는다. 응답이 필요 없는데 보내는 쪽이 훨씬 치명적이다.

이 설정을 Respond로 켜더라도, ACK는 아래 두 조건을 **모두** 만족할 때만 나간다.

1. **담당 슬롯일 것** — 카메라 슬롯 1~7 안이면서 IP가 설정된 주소(`isOwnedSlot()`).
2. **실제로 처리한 명령일 것** — `translatePelcoAndForward()`가 카메라로 명령을 하나라도
   내보냈을 때만(반환값 `true`). 모르는 Extended Command(`CMND2` bit0=1), 아직 미구현인
   Query(0x51/0x53/0x55), 비트가 하나도 안 켜진 패킷은 ACK 없이 무시한다.

2번이 필요한 이유는 두 가지다. ACK는 "받아서 처리했다"는 뜻인데 무시한 명령에 보내면 거짓
신호가 되고, ACK 한 번마다 RS485를 4바이트 점유하면서 그동안 수신도 막힌다(DE/RE가 송신
쪽으로 넘어가므로). ZU-EPC7000의 `CMND2=0xD3` 폴링처럼 쉬지 않고 들어오는 명령에 매번
응답하면 그 손실이 계속 누적된다.

ACK 전송 시점도 번역/전달 **뒤**로 옮겼다 — 그래야 ACK가 "전달했다"는 뜻과 실제로 일치한다. IP가 비어 있는 슬롯이나 범위 밖 주소는 게이트웨이가 담당하는
대상이 아니므로 침묵한다 — 그 주소의 실물 Pelco 카메라가 스스로 응답할 몫이다. 덕분에
"IP 카메라(FoMaKo)에는 ACK를 주고, 같은 버스의 실물 카메라(ED-P)에는 안 주는" 혼재 구성이
전역 설정 하나로 가능하다.

### 12.2.1 Camera Response Mode ("10. Set Camera Response Mode")

앞의 Pelco Response Mode가 "게이트웨이가 **지어낸** 응답"이라면, 이쪽은 "**IP 카메라가 실제로
보낸** 응답을 RS485로 되돌릴지"를 정한다. 기본값은 **none**이다.

| 값 | 동작 |
| --- | --- |
| `none` | 카메라 응답을 버린다 (기본값) |
| `synthetic` | 게이트웨이가 VISCA ACK/Completion을 합성해 돌려준다 — **VISCA 입력 경로 전용** |
| `forward` | 카메라가 보낸 바이트를 그대로 RS485로 내보낸다 |
| `forward_rewrite` | `forward`와 같되 첫 바이트를 `0x90 \| 카메라번호`로 바꾼다 |

**입력이 Pelco 계열(`Pelco-D`/`Pelco-P`/`Autodetect`)이면 `forward`/`forward_rewrite`를 골라도
RS485로 내보내지 않는다.** raw VISCA 바이트는 컨트롤러가 해석하지 못할 뿐 아니라, VISCA 응답의
종료 바이트 `0xFF`가 Pelco-D의 SYNC 바이트와 같아서 **같은 버스에 물린 다른 Pelco 장비가 그
자리에서 새 프레임을 시작해버린다** — 뒤이어 오는 진짜 명령의 앞부분을 그 유령 프레임이 삼켜서
통째로 깨진다. 실측 예: FoMaKo(VISCA 주소 6)의 ACK은 `E0 41 FF`, Completion은 `E0 51 FF`로
둘 다 `0xFF`로 끝난다.

Pelco 입력에서는 대신 Debug Mode에 `Camera response from ...: E0 41 FF (not forwarded - ...)`
로 찍어주므로, 카메라가 응답하는지 확인하는 진단 용도로는 그대로 쓸 수 있다. Pelco 쪽 ACK은
위의 Pelco Response Mode가 이미 즉시 합성해 돌려주고 있어, VISCA ACK/Completion을 중계해봐야
컨트롤러에 새로 줄 정보도 없다. 값이 실린 조회 응답을 Pelco Extended Response로 재포장하는
로직은 아직 구현되어 있지 않다 (`doc/pelcoD_command.md` 10절, 후속 작업).

`synthetic`은 `handleViscaPacket()` 경로에서만 동작한다 — Pelco 입력의 ACK는 위의 Pelco
Response Mode가 담당하므로, Pelco 구성에서 이 값을 골라도 아무 효과가 없다.

```text
1. Respond (synthetic ACK)
2. No response
```

Baudrate 선택값:

```text
1. 2400
2. 4800
3. 9600
4. 38400
5. 115200
```

### 12.2.2 핀 입력 유효성 검사

RX/TX/DE-RE와 Status LED 핀 입력에 같은 규칙이 적용된다. 규칙은
[Rs485PinValidation.cpp](src/Rs485PinValidation.cpp)에 한 벌만 있고 Serial 메뉴와 웹
설정 화면이 공유한다 — 한쪽만 고치면 두 UI가 서로 다른 핀을 허용/거부하게 된다.

**두 보드의 제약이 거의 겹치지 않는다.** 실제 목록은 [BoardProfile.h](src/BoardProfile.h)에 있다.

| 구분 | ESP32-C3 Super Mini | ESP32 클래식 |
| --- | --- | --- |
| 유효 범위 | GPIO0~21 | GPIO0~39 |
| 사용 불가 | 11~17 (내장 SPI 플래시), 18~19 (USB D-/D+) | 1, 3 (UART0 콘솔), 6~11 (내장 SPI 플래시) |
| 입력 전용 (TX/DE-RE 불가) | 없음 | 34~39 |
| 스트래핑 경고 | 2, 8, 9 | 0, 2, 5, 12, 15 |
| 부팅 로그 경고 | 20, 21 | 없음 (해당 핀이 이미 사용 불가) |

- 아무 값도 입력하지 않고 Enter만 누르면 변경 없이 취소된다.
- 상태 LED에 배정된 핀은 RS485로 쓸 수 없고, 그 반대도 마찬가지다.
- **사용 불가**에 걸리면 에러 메시지와 함께 다시 입력받는다. 이때도 빈 입력으로 취소할 수 있다.
- **경고**는 거부가 아니다. 값은 저장되고 메시지만 표시된다.
  - *스트래핑* : 외부 배선에 따라 부팅 모드가 바뀔 수 있다.
  - *부팅 로그* (C3의 GPIO20/21) : ROM 부트로더가 USB CDC와 무관하게 GPIO21로 115200bps
    부팅 로그를 뿜는다. 여기에 RS485 트랜시버를 물리면 **리셋할 때마다 버스에 쓰레기
    바이트가 실린다.** 핀이 13개뿐인 보드라 봉인하는 대신 경고로 남겼다 — DE/RE에 외부
    풀다운을 달아 부팅 중 드라이버를 꺼두면 실제로는 문제가 없다.

### 12.2.3 Status LED Polarity ("a. Set Status LED Polarity")

```text
1. Active High (HIGH = on) - typical external LED to GND
2. Active Low  (LOW = on)  - ESP32-C3 Super Mini onboard LED (GPIO8)
```

3.4절 참고. 숫자 항목이 이미 1~9로 다 차서 이 항목만 문자 `a`를 쓴다 — 기존 항목을
다시 번호 매기면 손에 익은 순서와 이 문서의 메뉴 캡처가 전부 어긋난다.

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
   1  | 0x81  | -               | 5678 | IP_VISCA_RAW_UDP  | preserve
   2  | 0x82  | -               | 5678 | IP_VISCA_RAW_UDP  | preserve
   3  | 0x83  | -               | 5678 | IP_VISCA_RAW_UDP  | preserve
   4  | 0x84  | -               | 5678 | IP_VISCA_RAW_UDP  | preserve
   5  | 0x85  | 192.168.1.105   | 5678 | IP_VISCA_RAW_UDP  | preserve
   6  | 0x86  | -               | 5678 | IP_VISCA_RAW_UDP  | preserve
   7  | 0x87  | -               | 5678 | IP_VISCA_RAW_UDP  | preserve

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
  Address Mode     : preserve

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
  Web RS485 TX         : 0
  Web RS485 TX Dropped : 0
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
| Web RS485 TX        | 웹 제어 패널이 RS485로 내보낸 마스터 프레임 수 (14절) |
| Web RS485 TX Dropped | 버스 유휴 창을 못 잡아 버린 웹 제어 프레임 수 — 계속 올라가면 버스가 그만큼 바쁘다는 뜻 |
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
  5. Raw Byte Monitor
  6. Send Test Command
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

"4. Live Packet Monitor"를 선택하면 Debug Mode가 꺼져 있어도 화면을 보는 동안만 메모리
상에서 자동으로 켜지고(flash에는 저장하지 않음), 아래와 같은 화면으로 전환되어 RS485 <->
IP VISCA 트래픽이 실시간으로 계속 출력된다. 아무 입력 없이 Enter만 누르면 스트리밍이
멈추고 진입 전 Debug Mode 값(ON/OFF)을 그대로 복원한 뒤 이전 메뉴(5. Debug Mode)로
돌아간다 — 즉 Live Packet Monitor를 한 번 봤다고 재부팅 후에도 Debug Mode가 계속 켜져
있지는 않는다.

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

### 12.5.2 Raw Byte Monitor

Live Packet Monitor는 프로토콜(VISCA/Pelco-D/Pelco-P) 파싱과 체크섬 검증까지 통과한 패킷만
보여준다 — 체크섬이 틀리면 Malformed Packet 카운터만 올라갈 뿐, 어떤 바이트가 왔는지는
전혀 보여주지 않는다. 배선이 잘못됐거나, Baudrate가 안 맞거나, Input Protocol 설정이 실제
컨트롤러가 보내는 프로토콜과 다를 때는 Live Packet Monitor가 계속 조용해서 "신호가 아예
없는 건지, 신호는 있는데 해석이 안 되는 건지" 구분이 안 된다. Raw Byte Monitor는 이럴 때
쓴다 — 파싱/체크섬 결과와 무관하게 RS485로 들어오는 바이트를 전부 그대로 hex로 보여준다.

"5. Raw Byte Monitor"를 선택하면 Debug Mode 여부와 무관하게 아래 화면으로 전환되어 RS485
바이트가 실시간으로 계속 출력된다. 바이트가 50ms(`RAW_MONITOR_GAP_MS`) 이상 끊기면 줄을
나눠 다음 버스트를 새 줄에 표시한다. 아무 입력 없이 Enter만 누르면 스트리밍이 멈추고
이전 메뉴(5. Debug Mode)로 돌아간다.

```text
============================================================
 5.5 Raw Byte Monitor
============================================================
Streaming raw RS485 bytes below, regardless of protocol/checksum.
Useful for diagnosing wiring/baudrate/protocol mismatches.
Press Enter (no input) to return to Debug Mode menu.
------------------------------------------------------------
[RAW] FF 01 00 08 20 00 29
[RAW] FF 01 00 04 20 00 25
[RAW] FF 06 00 D3 19 E6 D8
[TX ] FF 06 50 D7 19 01 47
```

`[RAW]`는 **버스에서 읽어들인** 바이트, `[TX ]`는 **게이트웨이가 내보낸** 패킷이다. 둘을
나눠 찍는 이유가 있다 — 게이트웨이는 자기가 보낸 바이트를 자기 RX로 되들을 수 없다.
`writePacket()`이 DE/RE를 송신 쪽으로 올리는 동안 트랜시버의 수신부가 꺼지기 때문이다.
그래서 수신 바이트만 echo하면 우리 응답이 화면에서 통째로 사라져, 다른 카메라 응답은
같은 줄에 보이는데 게이트웨이가 담당하는 주소만 응답이 없는 것처럼 보인다. `Rs485Port`의
`setTxEcho()` 훅으로 송신 패킷을 직접 찍어 그 공백을 메운다 (실제 송신이 끝난 뒤에
호출되므로 버스 타이밍에는 영향이 없다).

이 예는 Pelco-D 프레임(`FF 01 00 08 20 00 29` = Tilt Up, `FF 01 00 04 20 00 25` = Pan Left)이
정상적으로 들어오고 있다는 뜻이다. 만약 Live Packet Monitor에서는 아무것도 안 보이는데
여기서는 바이트가 보인다면, RS485 배선/Baudrate 자체는 정상이고 Input Protocol 설정이
실제 들어오는 프로토콜과 다르다는 뜻이다 (또는 노이즈로 체크섬이 계속 깨지는 경우).
반대로 여기서도 아무것도 안 보인다면 배선, Baudrate, 또는 RX/TX 핀 설정 자체를 의심해야
한다.

바이트가 보이긴 하는데 매번 똑같은 값으로 깨져서 들어온다면 **신호 극성(Signal Inversion)**을
의심해야 한다. Pelco-D 프레임은 반드시 `FF`로 시작하는데, 극성이 뒤집혀 있으면 그 `FF`가
`00`으로 읽히고 뒤 바이트들은 한 비트씩 밀린 보수값이 되어 아래처럼 찍힌다:

```text
[RAW] 00 BE 59 DF 45
[RAW] 00 BE 59 DF 45
```

`FF`가 한 번도 안 보이고 `00`으로 시작하는 고정 패턴이 반복된다면 12.2의 "8. Set Signal
Inversion"을 반대로 바꿔보면 된다 (또는 하드웨어에서 A/B를 바꿔 결선한다).

### 12.5.3 Send Test Command

지금까지의 도구(Live/Raw Monitor)는 모두 **수신**만 관찰한다. 정작 "RS485 배선이 양방향으로
제대로 동작하는지", "버스에 있는 무언가가 응답을 하긴 하는지"를 확인하려면 게이트웨이가
직접 뭔가를 먼저 보내볼 필요가 있다. "6. Send Test Command"는 게이트웨이가 평소와 반대로
**컨트롤러 역할**을 잠깐 맡아, `Query Pan Position` 조회 명령(주소 1 고정)을 RS485로 내보낸다.
Pelco-D/Pelco-P 둘 다 이 명령에 대한 응답 포맷이 정의되어 있어([pelcoD_command.md](pelcoD_command.md)
5절, [pelcoP_command.md](pelcoP_command.md) 5절) "응답이 오는지" 테스트하기에 적합하다.

```text
6. Send Test Command

Sends a Query Pan Position command (address 1) out on RS485 so you can
check whether anything on the bus responds. Switches to Raw Byte Monitor
right after sending so the response (if any) is visible either way, even
if it isn't a well-formed Pelco-D/P reply.
1. Pelco-D
2. Pelco-P
> 1
Sent (Pelco-D): FF 01 00 51 00 00 52
Watching for a response (Raw Byte Monitor)...
```

전송 직후 자동으로 Raw Byte Monitor 화면으로 전환되므로, 응답이 Pelco-D/P 포맷에 맞든 안 맞든
(체크섬이 깨졌어도, 아예 다른 프로토콜이어도) 뭐라도 돌아오면 hex로 그대로 보인다. 아무것도
안 보이면 그 방향(수신 쪽 또는 응답을 보낼 장치 자체)에 문제가 있다는 뜻이다.

주의: 이건 RS485 버스에 실제로 명령을 내보내는 동작이다. 컨트롤러가 아니라 게이트웨이가
"Query Pan Position"을 보내는 것이므로, 버스에 연결된 카메라(예: EDIS ED-P 계열)가 있다면
그 카메라의 Pan 위치를 실제로 조회하게 된다 — 이동 명령이 아니라 조회 명령이라 카메라가
움직이지는 않지만, 정상적인 컨트롤러-카메라 통신 흐름을 잠깐 끼어드는 셈이니 운영 중에는
주의해서 사용한다.

---

## 13. Web Config Server

USB Serial에 물리적으로 접근할 수 없는 상황을 위한 두 번째 설정 인터페이스 —
이미 설치돼서 USB 케이블을 다시 꽂기 번거로운 장비를 위함이다.
`SerialMenu`와 동일한 `RoutingTable`/`Storage`/`Diagnostics`를 그대로
참조하므로(`WebConfigServer.h/.cpp`) 두 UI가 항상 같은 flash 설정을 보고
쓴다 — 한쪽에서 바꾼 값이 다른 쪽에도 바로 반영된다.

동기 방식 `WebServer`(arduino-esp32 core 내장, 별도 `lib_deps` 불필요)를
쓰며, HTML은 파일시스템 없이 `WebConfigServer.cpp` 안에 C++ 문자열로 직접
들어있다(SerialMenu.cpp가 화면 전체를 한 파일에 담는 것과 같은 방식).

페이지는 `sendPage()`가 **chunked로 흘려보낸다.** 완성된 페이지를 String 하나에 쌓아
`send()`에 넘기면 조립하는 동안 그 String이 여러 번 재할당되고, 완성본과 호출부의
`bodyHtml`이 한동안 동시에 힙에 올라간다. 조각으로 보내면 페이지 전체를 담는 String
자체가 없어지고, 변하지 않는 머리말·CSS는 힙을 거치지 않고 플래시에서 곧바로 소켓으로
나간다. ESP32-C3는 Wi-Fi/lwIP와 같은 메모리를 나눠 쓰는 데다 싱글코어라 이 차이가
여유 힙과 `loop()` 시간에 그대로 반영된다.

### 13.1 AP+STA 동시 운용

`connectWifi()`가 `WiFi.mode(WIFI_AP_STA)`로 STA(평소 Wi-Fi)와 AP를 동시에
띄운다. AP는 STA 연결 성공 여부와 무관하게 **항상 켜져 있다** — Wi-Fi가 아예
설정 안 됐거나 끊긴 상태에서도 웹 UI로 접근할 수 있게 하기 위함이다.

- AP SSID/Password는 **Serial("5./6." 12.1절) 또는 웹(`/network`, 13.2절)에서 직접
  바꿀 수 있다** — 저장 즉시 `applyApSettings()`(`GatewayActions.h/.cpp`, 13.4절)가
  `WiFi.softAP()`를 재적용해서 재부팅 없이 반영된다.
- AP SSID 기본값: `RS485Gateway-XXXX` (XXXX는 MAC 주소 뒷자리 4자리, 기기별로
  다름). 최초 부팅 시(`cfg.wifi.apSsid`가 비어있을 때) `WebConfigServer::begin()`이
  한 번만 만들어서 flash에 저장하고, 그 뒤로는 계속 저장된 값을 쓴다 — 사용자가
  직접 바꾸기 전까지는 이 자동 생성값이 유지된다.
- AP Password 기본값: `config.h`의 `AP_PASSWORD_DEFAULT`(`RoutingTable::applyDefaults()`가
  굽는다)
- AP IP: ESP32 기본값 `192.168.4.1` (고정, 설정 불가)
- 웹 페이지 자체에는 로그인이 없다(신뢰된 LAN/AP 전제) — AP Wi-Fi 접속
  비밀번호가 최소한의 방어선이다.

Serial 메뉴의 "1. Network Settings" 화면에도 AP SSID/IP가 표시된다(12.1절) —
Serial로만 접근 가능한 상태에서도 웹 UI로 넘어갈 방법을 알 수 있도록.

### 13.2 페이지 구성

Serial 메뉴 화면과 1:1로 대응하되, 여러 단계 프롬프트 대신 폼 하나로 평탄화했다.

| 경로 | Serial 대응 | 비고 |
|---|---|---|
| `GET /` | Main Menu 상태 블록 | Board(어느 보드용 펌웨어인지), Wi-Fi(STA/AP) 상태, Debug Mode, 각 페이지 링크 |
| `GET`/`POST /network` | Network Settings | SSID(**직전 스캔** 드롭다운 + 직접 입력, 13.5절), Password(빈 칸 = 기존 유지), DHCP, Static IP/Gateway/Subnet, Retry 버튼, AP SSID/Password(별도 폼, `POST /network/ap`) |
| `GET`/`POST /rs485` | RS485 Settings | Board/UART 표시, Baudrate, Signal Inversion, RX/TX/DE-RE Pin(서버에서 `validateRs485Pin()`으로 검증), Input Protocol, Pelco Response Mode, Camera Response Mode, Status LED Pin/Logic |
| `GET /routing` | Routing Table | CAM1~7 목록 |
| `GET`/`POST /routing/cam?n=N` | Camera Detail | IP(빈 칸 = 삭제)/Port/Protocol/Address Mode/Auto Power Control |
| `GET /counters` | Counters | 2초 자동 새로고침 |
| `GET /debug` | Debug Mode | Debug ON/OFF, Last Packets, Send Test Command |
| `GET /debug/live` | Live Packet Monitor | 1초 자동 새로고침. Serial과 달리 Debug Mode를 자동으로 켜지 않음 — `/debug`에서 먼저 켜야 함 |
| `GET /debug/raw` | Raw Byte Monitor | 1초 자동 새로고침, 파싱/체크섬과 무관하게 항상 채워지는 별도 로그(13.3절) |
| `POST /debug/test-command` | Send Test Command | Pelco-D/P Query Pan Position 프로브 전송 후 `/debug/raw`로 이동 |
| `GET`/`POST /update` | (Serial 대응 없음) | 펌웨어 업데이트(OTA, 13.4절). 현재 빌드 시각/슬롯 표시 + `.bin` 업로드 폼 |
| `GET`/`POST /factory-reset` | Factory Reset | 경고 문구 + 확인 버튼(POST 전용, 타이핑 확인 대신 실수로 못 누르게 별도 페이지+버튼 클릭) |
| `GET /control` | (Serial 대응 없음) | PTZ 제어 패널(14절). 화면 원본은 `web/control.html`이고 빌드 시 gzip PROGMEM 배열로 구워진다 |
| `GET /api/state` | | 제어 패널이 1초마다 폴링하는 상태 JSON |
| `POST /api/cmd` | | 제어 명령 하나. **AP 접속이면 403** |

자동 새로고침은 JS 없이 `<meta http-equiv="refresh">`만 쓴다 — 임베디드
환경에서 가장 단순하고 확실하게 동작하는 폴링 방식이라 이걸 기본으로 택했다.

### 13.3 Raw Byte Monitor 백엔드

Serial의 Raw Byte Monitor(`echoRawByte()`)는 그 화면이 켜져 있을 때만 Serial로
직접 echo한다. 웹 페이지가 폴링할 데이터가 항상 있으려면 화면 상태와 무관한
별도 저장소가 필요해서, `Diagnostics`에 `pushRawLog()`/`recentRawLog()`라는
전용 링버퍼를 추가했다(파싱된 RX/TX 로그와 섞이지 않게 분리). `main.cpp`가
Input Protocol이나 어느 화면이 열려 있는지와 무관하게 RS485 바이트가 들어올
때마다 항상 이 로그를 채운다.

### 13.4 Serial과 공유하는 로직

두 가지는 Serial/Web 양쪽에서 동일하게 동작해야 하는 규칙이라 별도 파일로 뽑아
공유한다(한쪽만 고치면 두 UI가 다르게 동작하는 걸 방지):

| 파일 | 내용 |
|---|---|
| `Rs485PinValidation.h/.cpp` | GPIO 예약/입력전용/스트래핑 핀 검사 |
| `GatewayActions.h/.cpp` | Factory Reset 시퀀스, Pelco-D/P 테스트 커맨드 패킷 생성, AP SSID/Password 적용(`applyApSettings()`) |

### 13.4.1 펌웨어 업데이트 (OTA)

`GET /update`가 현재 펌웨어 정보를, `POST /update`가 `.bin` 업로드를 받는다.

**파티션은 바꾸지 않았다.** PlatformIO 기본값인 `default.csv`가 이미 OTA 2슬롯이다:

```
nvs,     data, nvs,   0x9000,   20K     설정 - app과 별개라 업데이트해도 안 날아간다
otadata, data, ota,   0xe000,    8K     다음에 어느 슬롯으로 부팅할지
app0,    app,  ota_0, 0x10000, 1280K    지금 도는 펌웨어 (약 877KB, 68%)
app1,    app,  ota_1, 0x150000,1280K    새 펌웨어가 기록될 자리
```

파티션을 바꿔야 했다면 현장 장비를 USB로 한 번 완전히 지우고 다시 구워야 했을 텐데,
그럴 필요가 없다. 설정도 NVS에 그대로 남는다 — `Storage::load()`가 옛 레이아웃을
받아주므로(10.0.1절) 설정 항목이 늘어난 펌웨어로 올려도 Wi-Fi/카메라 IP를 다시 넣을
필요가 없다.

#### 칩이 맞는지 직접 검사한다

**`Update` 라이브러리는 이걸 안 해준다.** `Updater.cpp`의 `_verifyHeader()`는 매직
바이트 `0xE9` 하나만 보는데, 그 값은 ESP32 계열 전부가 같다. 그래서 클래식 ESP32용
바이너리를 C3에 올려도 **검사를 전부 통과하고 기록이 끝난 뒤 부팅 파티션까지 바꾼다.**
칩이 안 맞는다는 걸 부트로더가 알아채는 건 재부팅한 다음이고, 그때는 이미 부팅 루프다 —
천장에 달린 장비를 내려 USB로 다시 구워야 한다.

이 프로젝트는 보드를 둘 빌드하므로 두 `firmware.bin`이 나란히 놓인다. 헷갈릴 만한 게
아니라 헷갈리게 되어 있어서, `verifyFirmwareHeader()`가 **flash에 쓰기 전에** 첫 조각의
이미지 헤더를 직접 본다:

| 오프셋 | 내용 | 검사 |
| --- | --- | --- |
| `[0]` | 매직 `0xE9` | 아니면 "ESP32 펌웨어 이미지가 아니다" |
| `[12..13]` | chip id (LE) | 빌드의 `CONFIG_IDF_FIRMWARE_CHIP_ID`(C3 `0x0005`, 클래식 `0x0000`)와 다르면 거부 |

거부되면 아무것도 기록되지 않고, 어느 보드용 이미지를 올렸는지가 화면에 그대로 나온다.

#### 알아둘 것

- **AP에서도 허용한다.** 제어 패널의 PTZ 조작은 `WebControl::controlAllowed()`가 AP를
  막지만(13.6절과 같은 이유), 펌웨어 업로드는 막지 않는다 — 운영자의 결정이다. 설정
  화면 전체가 그렇듯 로그인이 없으므로, **AP에 접속할 수 있는 사람은 펌웨어를 바꿀 수
  있다.** AP 비밀번호가 유일한 문턱이다.
- **업로드 중에는 RS485를 처리하지 않는다.** flash 쓰기 동안 `loop()`가 멈추므로,
  움직이던 카메라는 계속 움직인다. 업로드 전에 Stop을 자동으로 보내지는 않는다.
- **롤백은 없다.** `Update`가 크기와 MD5는 검증하지만 "부팅은 되는데 곧 죽는 펌웨어"는
  못 거른다. 그런 이미지를 올리면 USB로 다시 구워야 한다. 업로드 후 페이지가 20초 뒤
  자동으로 돌아오니, **Build 시각이 바뀌었는지 반드시 확인**한다 — 그게 새 펌웨어가
  실제로 부팅했다는 유일한 증거다.
- 진행률 표시는 없다. 설정 화면은 JS를 쓰지 않는다는 방침(13.2절)이라 평범한 폼
  POST이고, 업로드가 끝날 때까지 브라우저가 대기 표시만 낸다.

### 13.5 알려진 제약

- Wi-Fi 스캔(`/network`)은 **직전 스캔 결과**를 보여준다. 처음 열면
  `(scanning - reload this page in a few seconds)`가 뜨고, 새로고침해야 목록이 나온다.
  - 예전에는 GET마다 `WiFi.scanNetworks()`를 동기 호출했는데, 그러면 스캔이 끝날 때까지
    2~4초 `loop()`가 멈춘다. RS485 수신 링버퍼가 9600bps 기준 약 1초분이라 그 사이
    프레임이 유실되고, 거기에 Stop 명령이 섞여 있으면 **카메라가 계속 돈다.** 설정
    화면을 여는 것만으로 운용 중인 게이트웨이가 명령을 흘리는 셈이었다 — 예배 중에
    누가 폰으로 열 수 있는 화면이라 실제로 일어날 수 있는 일이다.
  - Serial 메뉴의 스캔은 여전히 동기다(12.1절). 결과를 번호로 매겨 바로 다음 입력에서
    고르는 구조라 비동기로 바꾸면 두 단계로 갈라지고, 콘솔 앞에 사람이 서서 설정하는
    중이라는 점도 다르다. 대신 "RS485 input is not processed for a few seconds"를
    함께 출력해 숨기지 않는다.
  - 마지막 스캔 결과는 다음 스캔이 시작될 때까지 메모리에 남는다(AP 스무 개 남짓이면
    1~2KB). `scanDelete()`를 부르면 "직전 결과를 보여준다"가 성립하지 않는다.
- "Retry Wi-Fi Connection"은 최대 `WIFI_CONNECT_TIMEOUT_MS`(15초)까지 요청을
  블로킹한다 — Serial의 "4. Retry Wi-Fi Connection"과 동일한 동작.
- Response Mode(VISCA `NONE`/`SYNTHETIC`/`FORWARD`/`FORWARD_REWRITE`)는 Serial
  메뉴에도 노출되어 있지 않아 웹에도 넣지 않았다 — 둘 다 `applyDefaults()`가
  정하는 기본값(`NONE`)만 쓸 수 있다.

---

## 14. Web PTZ Controller

ZU-EPC7000 물리 컨트롤러를 그대로 옮긴 제어 패널이다. 브라우저 주소창에 게이트웨이의 STA
주소를 치면 열린다 — `http://192.168.0.50/control`. 설정 화면과 같은 서버·같은 포트를 쓰므로
주소는 하나뿐이고, 설정 화면 상단 메뉴 오른쪽 끝의 **Control Panel** 버튼으로도 갈 수 있다. **전체 문서는
[Web_controller.md](Web_controller.md)** 에 있고, 여기서는 요점만 적는다.

- **STA 주소로 접속해야 제어된다.** AP(`192.168.4.1`)로 들어오면 페이지는 열리지만
  조작 UI가 잠기고 `POST /api/cmd`도 403으로 거절된다 — 제어 화면은 설정 화면과 달리
  누구나 폰으로 열어두는 화면이라, AP 비밀번호 하나를 권한 경계로 삼지 않는다.
- **경로는 슬롯의 IP 유무로 갈린다.** IP가 설정된 슬롯은 IP VISCA로, 비어 있는 슬롯은
  RS485 Pelco-D 마스터 프레임으로 나간다. 설정 필드는 새로 만들지 않았다(10.1절의 이유).
- **RS485로 나갈 때는 버스가 30ms 이상 조용할 때만 끼어든다**(`WEB_TX_BUS_IDLE_MS`).
  이 버스에는 ZU-EPC7000이 이미 마스터로 있고, 게이트웨이는 자기 송신 중에 버스를 들을 수
  없어 충돌을 감지할 방법이 없다. 조회(`D3`)는 절대 내보내지 않는다 — 그 답을 컨트롤러가
  자기 질문의 답으로 오해한다.
- **상태 표시는 추가 트래픽 없이 얻는다.** 컨트롤러가 폴링하는 `D3` 질문과 카메라의 `D7`
  응답을 둘 다 엿들어(`sniffBusFrame()`) 모드 캐시를 채운다. 한 번도 관측 못 한 항목은
  `-`로 표시한다.
- **이동은 임대다.** 브라우저가 300ms마다 갱신하지 않으면 700ms 뒤 게이트웨이가 스스로
  Stop을 만든다(`WEB_HOLD_TIMEOUT_MS`) — 탭이 죽거나 Wi-Fi가 끊겨도 카메라가 계속 돌지
  않게 하기 위함이다.
- 웹에서 바꾼 모드는 기존 `modeCache`에 반영되므로 **물리 컨트롤러 LCD도 따라온다.**

MENU 키만 비활성이다 — 그 키가 보내는 바이트를 한 번도 캡처하지 못했다
([Web_controller.md 4절](Web_controller.md)).

패널이 쓰는 `POST /api/cmd` / `GET /api/state`는 브라우저 전용이 아니라 그냥 HTTP라,
외부 프로그램이나 스크립트도 그대로 부를 수 있다. 그 경로를 **외부 제어의 공식**
인터페이스로 확정했고, PC 쪽 래퍼 CLI [`tools/ptz.py`](../tools/ptz.py)가 그 위에 있다
(`ptz.py 6 preset goto 3`). 명령 어휘, deadman 규약, 속도 한계, 검토했다가 접은 대안들
(UDP Pelco-D, 텍스트 프로토콜, VISCA over IP)은 **[cli_interface.md](cli_interface.md)** 에 있다.

---

## 15. 구현 제외 항목

다음 기능은 구현하지 않는다.

| 제외 항목      | 이유                                        |
| -------------- | ------------------------------------------- |
| ONVIF 제어     | 구현 복잡도가 높고 현재 목적은 VISCA 변환임 |
| 영상 스트리밍  | 본 장치는 제어 신호 변환 장치임             |
| RTSP 처리      | 제어 기능과 무관                            |
| NVR/VMS 연동   | 범위 밖                                     |

---

## 16. 소스 구조

```text
/web
  control.html          제어 패널 화면 원본 (빌드 시 gzip PROGMEM으로 구워짐)
  api/state             브라우저 미리보기용 가짜 응답 (web/README.md)
/tools
  embed_web.py          web/*.html -> src/generated/WebAssets.h (PlatformIO pre-build)
  ptz.py                외부 제어용 CLI (doc/cli_interface.md)
/src
  main.cpp
  config.h
  BoardProfile.h        보드마다 다른 하드웨어 사실(UART 번호, 기본 핀, 예약 핀) 전부
  ViscaParser.h / .cpp
  PelcoDParser.h / .cpp
  PelcoPParser.h / .cpp
  Rs485Port.h / .cpp
  IpViscaClient.h / .cpp
  SonyViscaClient.h / .cpp
  RoutingTable.h / .cpp
  SerialMenu.h / .cpp
  WebConfigServer.h / .cpp
  WebControl.h / .cpp
  Rs485PinValidation.h / .cpp
  GatewayActions.h / .cpp
  Diagnostics.h / .cpp
  Storage.h / .cpp
  StatusLed.h / .cpp
```

헤더도 전부 `src/`에 있다 — PlatformIO 기본 골격의 `include/`, `lib/`, `test/`는 이
프로젝트에서 쓰지 않아 지웠다(내용 없는 안내문만 들어 있었다). 빌드 산출물은 `.pio/`가
아니라 프로젝트 밖에 쌓인다(3.1절).

모듈별 역할:

| 모듈                | 역할                                                    |
| ------------------- | ------------------------------------------------------- |
| BoardProfile        | 보드 의존 상수의 유일한 출처 — ESP32 클래식 / ESP32-C3 (3.1절) |
| ViscaParser         | `0xFF` 기준 VISCA 패킷 파싱                              |
| PelcoDParser        | Pelco-D 고정 7바이트 프레임 파싱(합산 체크섬)            |
| PelcoPParser        | Pelco-P 고정 8바이트 프레임 파싱(XOR 체크섬)             |
| Rs485Port           | RS485 UART(클래식 UART2 / C3 UART1) 및 DE/RE 제어         |
| IpViscaClient       | Raw UDP/TCP IP VISCA 전송 (UDP 소켓은 로컬 포트 5678에 bind) |
| SonyViscaClient     | Sony VISCA over IP framing 및 전송                       |
| RoutingTable        | 카메라 1~7 IP/Port/Protocol/Address Mode 관리            |
| SerialMenu          | USB Serial 메뉴 입력/출력                                |
| WebConfigServer     | AP+STA 웹 설정 서버(13절) — SerialMenu와 같은 백엔드 공유 |
| WebControl          | 웹 PTZ 제어 패널(14절) — `/control` 화면과 `/api/*`, AP 차단 판정 |
| Rs485PinValidation  | GPIO 핀 검증 규칙(Serial/Web 공유) — 실제 값은 BoardProfile |
| GatewayActions      | Factory Reset, Pelco 테스트 커맨드 생성, AP 설정 적용(Serial/Web 공유) |
| Diagnostics         | 카운터, 최근 패킷/raw 바이트 로그, 디버그 출력 관리      |
| Storage             | Preferences/NVS 저장 및 로드                             |
| StatusLed           | 상태 LED 제어 (핀/극성 런타임 설정, 3.4절)               |

---

## 17. Claude Code 구현 지시사항

이 프로젝트는 Arduino ESP32 펌웨어로 구현한다.

핵심 요구사항:

1. USB Serial 콘솔은 메뉴와 디버그 전용으로 사용한다 — 클래식은 UART0, C3는 네이티브 USB CDC다(3.1절).
2. RS485 패킷은 콘솔과 겹치지 않는 독립 UART로 수신한다(클래식 UART2 / C3 UART1, VISCA/Pelco-D/Pelco-P, 12.2절).
3. RS485 방향 제어 핀 기본값은 보드가 정한다(클래식 GPIO27 / C3 GPIO6, `BoardProfile.h`).
4. RS485 기본 모드는 Receive이다.
5. VISCA 패킷은 `0xFF`를 기준으로 구분한다.
6. 패킷 첫 바이트 `0x81~0x87`을 카메라 1~7로 해석한다(VISCA 입력 기준 — Pelco 입력은 ADDR 바이트로 매핑, 12.2절).
7. 카메라 1~7 각각에 대해 IP, Port, Protocol, Address Mode를 저장한다.
8. 해당 카메라 번호에 IP가 설정되어 있으면 IP VISCA로 전송한다.
9. 해당 카메라 번호에 IP가 없으면 무시한다.
10. `0x88` Broadcast가 들어오면 IP가 설정된 모든 카메라에 전송한다.
11. 기본 Address Mode는 `preserve`이다.
12. 기본 Protocol은 `IP_VISCA_RAW_UDP`이다.
13. 기본 Port는 5678이다.
14. AP는 STA 연결 여부와 무관하게 항상 켜둔다(`WIFI_AP_STA`, 13.1절).
15. Wi-Fi STA 연결 실패 시에도 Serial 메뉴와 웹 AP는 계속 사용할 수 있어야 한다.
16. Wi-Fi STA 연결 실패 시 주기적으로 재접속을 시도한다.
17. ONVIF는 구현하지 않는다.
18. Debug Mode에서는 RS485 수신과 IP 전송을 실시간으로 출력한다.
19. 설정은 Preferences/NVS에 저장하고 재부팅 후 복원한다.
20. Serial 시작 메뉴는 다음 6개 항목만 사용한다.

```text
1. Network Settings
2. RS485 Settings
3. Routing Table
4. Counters
5. Debug Mode
6. Factory Reset
```

가능하면 메뉴 처리는 non-blocking에 가깝게 구현한다. Serial 메뉴가 표시되어 있어도 RS485 패킷 수신과 IP 전송이 중단되지 않아야 한다.