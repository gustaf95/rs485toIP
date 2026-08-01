# Pelco-D Command List

FUJIFILM SX800/SX801용 공식 Pelco-D 프로토콜 스펙(`pelco-d_protocol_specification_for_sx800-801_v2.00.4_eng.pdf`)을
기준으로 프로토콜 구조를 정리하고, RS485 PTZ 컨트롤러(ZU-EPC7000 계열) 커맨드 표를 그 아래에 덧붙인다. 추가로
EDIS ED-P 시리즈 카메라 매뉴얼, ZU-EPC7000 컨트롤러 자체 매뉴얼(`ZU-EPC7000+System+PTZ+Controller+manual_kor_Ver1.85.pdf`),
그리고 실제 타겟 카메라인 FoMaKo의 매뉴얼(`fomako_manual.pdf`)을 교차 참조해 정리했다.

Pelco-P 프로토콜은 구조가 상당히 달라(시작 바이트, 체크섬 방식 등) 별도 문서 [`pelcoP_command.md`](pelcoP_command.md)로
분리했다. 자세한 이유는 7절 참고.

---

## 1. 프로토콜 개요

- Master-Slave 구조. Slave(카메라)는 Master(컨트롤러)의 요청 없이는 데이터를 보내지 않는다.
- Baudrate: 2400 / 4800 / 9600 / 19200 / 38400 / 115200
- 시리얼 포맷: Start bit 1, Data 8bit, Stop bit 1, Parity None
- 주소(ADDR) 범위는 벤더마다 다르다 (FUJIFILM은 1~31, FoMaKo는 1~15). 본 프로젝트는 VISCA 쪽 카메라
  슬롯 1~7에 맞춰 사용한다.
- 컨트롤러(ZU-EPC7000) 쪽 사양(채널별 프로토콜 개별 설정, 물리 계층 매핑 등)은 9절 참고.

## 2. Send Command 포맷 (공통)

| Byte | 1    | 2    | 3     | 4     | 5     | 6     | 7    |
| ---- | ---- | ---- | ----- | ----- | ----- | ----- | ---- |
| 필드 | SYNC | ADDR | CMND1 | CMND2 | DATA1 | DATA2 | CKSM |
| 값   | 0xFF | -    | -     | -     | -     | -     | -    |

- CKSM = **Byte2(ADDR)부터 Byte6(DATA2)까지의 합을 8bit로 자른 값**. (본 프로젝트의 `PelcoDParser`가
  구현한 체크섬 방식과 일치함)
- CMND1은 확장 명령, CMND2는 기본 동작(Pan/Tilt/Zoom/Focus 등) 명령으로 쓰인다.

## 3. Receive Command 포맷 (응답)

**주의: Pelco-D는 벤더마다 응답 유무와 포맷이 다르다.** 아래는 FUJIFILM 스펙 기준이며, 실제 사용 중인
컨트롤러(ZU-EPC7000)가 어떤 방식을 쓰는지는 실측(Live Packet Monitor)으로 확인이 필요하다.

- **General Response** (4 byte): `FF ADDR ALARMS(0x00) CKSM` — 명령을 받았다는 단순 ACK. FUJIFILM은
  Pan/Tilt 이동을 포함한 대부분의 명령에 이 응답을 요구한다.
  - FUJIFILM 스펙 4장: "SX800과 SX801은 명령을 큐잉하지 않으므로, 이전 명령의 Receive Command를
    받기 전에 다음 명령을 보내면 이전 명령은 폐기된다." → 이 카메라는 명령마다 응답을 기다리는
    엄격한 request-response 흐름이 필요하다.
- **Extended Response** (7 byte): `FF ADDR RESP1 RESP2 DATA1 DATA2 CKSM` — 받은 CMND1/CMND2를
  그대로 RESP1/RESP2에 되돌려주고, DATA1/DATA2에 조회 결과 등을 담는다.
- **Query Response** (18 byte): `FF ADDR DATA1..DATA15 CKSM` — Serial Number 조회처럼 데이터가 긴 경우.

## 4. Standard Command (비트 플래그)

Pelco-D의 Command1/Command2 바이트는 "명령 코드"가 아니라 **비트 플래그**다. 여러 비트를 동시에 세우면
대각선 이동(Up+Left 등) 같은 동시 동작을 표현할 수 있다.

**Byte3, CMND1**

| Bit7  | Bit6 | Bit5 | Bit4              | Bit3           | Bit2       | Bit1      | Bit0       |
| ----- | ---- | ---- | ----------------- | -------------- | ---------- | --------- | ---------- |
| Sense | 0    | 0    | Auto/Manual Scan  | Camera On/Off  | Iris Close | Iris Open | Focus Near |

**Byte4, CMND2**

| Bit7      | Bit6      | Bit5      | Bit4 | Bit3 | Bit2 | Bit1  | Bit0    |
| --------- | --------- | --------- | ---- | ---- | ---- | ----- | ------- |
| Focus Far | Zoom Wide | Zoom Tele | Down | Up   | Left | Right | 항상 0 |

- Camera On/Off는 Sense(bit7)와 조합해서 쓴다: **ON = 0x88**(Sense=1 + bit3), **OFF = 0x08**(Sense=0 + bit3).
- Auto/Manual Scan도 동일 방식: ON = 0x90, OFF = 0x10.
- 아래 5절 표의 Pan/Tilt/Zoom/Focus 값들이 정확히 이 비트 배치와 일치함을 FUJIFILM 스펙으로 교차 확인했다.
- 대각선 이동(Upleft/Upright/DownLeft/DownRight)은 비트 OR 조합으로 표현된다 (예: Upleft = Up|Left =
  0x08|0x04 = 0x0C). FoMaKo 자체 Pelco-D 표(7절)에도 이 네 가지 조합이 별도 행으로 명시되어 있어
  실제로 벤더가 이 조합 방식을 지원함을 확인했다.
- FUJIFILM SX800/801 자체는 Bit3~Bit7(Camera On/Off, Iris, Scan, Sense)을 구현하지 않고 Focus Near(Bit0)만
  지원한다고 명시되어 있다 — 즉 비트 위치는 표준이어도 실제 지원 여부는 카메라 펌웨어마다 다르다.

## 5. RS485 PTZ 컨트롤러 커맨드 목록 (ZU-EPC7000 계열)

RS485 PTZ 컨트롤러 기준 Pelco-D 커맨드 셋. Address(Byte2)는 예시로 `01`을 사용.

| No  | Command                 | Byte1 | Byte2 | Byte3 | Byte4 | Byte5                 | Byte6                 | Byte7 |
| --- | ------------------------ | ----- | ----- | ----- | ----- | ---------------------- | ---------------------- | ----- |
| 1   | Pan Left                 | FF    | 01    | 00    | 04    | DATA1                  | DATA2                  | check |
| 2   | Pan Right                | FF    | 01    | 00    | 02    | DATA1                  | DATA2                  | check |
| 3   | Tilt Up                  | FF    | 01    | 00    | 08    | DATA1                  | DATA2                  | check |
| 4   | Tilt Down                | FF    | 01    | 00    | 10    | DATA1                  | DATA2                  | check |
| 5   | Zoom Tele                | FF    | 01    | 00    | 20    | 00                     | 00                     | check |
| 6   | Zoom Wide                | FF    | 01    | 00    | 40    | 00                     | 00                     | check |
| 7   | Focus Near               | FF    | 01    | 01    | 00    | 00                     | 00                     | check |
| 8   | Focus Far                | FF    | 01    | 00    | 80    | 00                     | 00                     | check |
| 9   | Set Preset               | FF    | 01    | 00    | 03    | 00                     | Preset ID              | check |
| 10  | Goto Preset              | FF    | 01    | 00    | 07    | 00                     | Preset ID              | check |
| 11  | Clear Preset             | FF    | 01    | 00    | 05    | 00                     | Preset ID              | check |
| 12  | Run Group                | FF    | 01    | 00    | 23    | 00                     | Zone ID                | check |
| 13  | Run Swing                | FF    | 01    | 00    | 1B    | 00                     | 00                     | check |
| 14  | Initialize Pan/Tilt      | FF    | 01    | 00    | 0F    | 00                     | 00                     | check |
| 15  | Aux On                   | FF    | 01    | 00    | 09    | 00                     | 01 ~ 06                | check |
| 16  | Aux Off                  | FF    | 01    | 00    | 0B    | 00                     | 01 ~ 06                | check |
| 17  | Set Pan Position         | FF    | 01    | 00    | 4B    | Msb Of Pan Position    | Lsb Of Pan Position    | check |
| 18  | Set Tilt Position        | FF    | 01    | 00    | 4D    | Msb Of Tilt Position   | Lsb Of Tilt Position   | check |
| 19  | Set Zoom Position        | FF    | 01    | 00    | 4F    | Msb Of Zoom Position   | Lsb Of Zoom Position   | check |
| 20  | Set Focus Position       | FF    | 01    | 00    | 5F    | Msb Of Focus Position  | Lsb Of Focus Position  | check |
| 21  | Query Pan Position       | FF    | 01    | 00    | 51    | 00                     | 00                     | check |
| 22  | Query Tilt Position      | FF    | 01    | 00    | 53    | 00                     | 00                     | check |
| 23  | Query Zoom Position      | FF    | 01    | 00    | 55    | 00                     | 00                     | check |
| 24  | Query Focus Position     | FF    | 01    | 00    | 61    | 00                     | 00                     | check |
| 25  | Response Pan Position    | FF    | 01    | 00    | 59    | Msb Of Pan Position    | Lsb Of Pan Position    | check |
| 26  | Response Tilt Position   | FF    | 01    | 00    | 5B    | Msb Of Tilt Position   | Lsb Of Tilt Position   | check |
| 27  | Response Zoom Position   | FF    | 01    | 00    | 5D    | Msb Of Zoom Position   | Lsb Of Zoom Position   | check |
| 28  | Response Focus Position  | FF    | 01    | 00    | 63    | Msb Of Focus Position  | Lsb Of Focus Position  | check |

- DATA1 = Pan Speed, DATA2 = Tilt Speed (Pan/Tilt 이동 커맨드 한정)
- check = Checksum (Byte2~Byte6 합산)
- 27번 Response Zoom Position은 FUJIFILM(5.2.11절)·FoMaKo 매뉴얼이 공통으로 `0x5D`를 명시하고 있어
  최초 표에 있던 `0x6D`를 오타로 보고 정정했다.

## 6. FUJIFILM 스펙과 교차 검증한 사항

- Pan/Tilt/Zoom/Focus 이동 값(1~8번 행)은 FUJIFILM의 Standard Command 비트 배치와 정확히 일치.
- 체크섬 계산 방식(Byte2~Byte6 합산) 일치.
- Query/Response Pan·Tilt·Zoom Position(0x51/0x59, 0x53/0x5B, 0x55/0x5D)의 "+8" 대응 패턴은
  FUJIFILM·FoMaKo 양쪽에서 확인됨.
- Set/Goto/Clear Preset(9~11번 행)은 FUJIFILM 스펙(5.2~5.13절 전체)에 대응 명령이 없음 — FUJIFILM
  SX800/801은 프리셋 기능 자체가 없는 것으로 보인다. 즉 "표준 Pelco-D 코어"라는 게 따로 있는 게
  아니라 카메라마다 지원 집합이 다르다는 뜻이며, ZU-EPC7000 컨트롤러 표는 그 나름의 벤더 규약이다.
- Query/Response Focus Position(24, 28번 행)은 FUJIFILM에서 완전히 다른 방식(Extended Response로
  CMND1/CMND2를 그대로 에코)을 쓰고 있어 직접 비교는 불가능. 다만 이 두 값(0x61/0x63)은 ZU-EPC7000
  표 자체에서 나온 것이라 그대로 유지한다.

---

## 7. FoMaKo 카메라(실제 타겟) 자체 Pelco-D/Pelco-P 지원 범위 — 가장 중요

`fomako_manual.pdf` 5.4절/5.5절에 FoMaKo 카메라 자체가 정의한 Pelco-D/Pelco-P 커맨드 표가 있다. 이건
FUJIFILM이나 ZU-EPC7000처럼 "참고용 다른 벤더"가 아니라 **이 프로젝트가 실제로 변환 대상으로 삼는
카메라 본인의 스펙**이므로, 5절의 ZU-EPC7000 28행 표 중 어떤 명령이 실제로 FoMaKo에서 먹히는지 여기서
가려낼 수 있다.

### 7.1 FoMaKo Pelco-D 지원 여부 (ZU-EPC7000 28행 표 기준 대조)

| No | Command | FoMaKo 지원 | 비고 |
|---|---|---|---|
| 1 | Pan Left | ✅ | `Left` 0x04 |
| 2 | Pan Right | ✅ | `Right` 0x02 |
| 3 | Tilt Up | ✅ | `Up` 0x08 |
| 4 | Tilt Down | ✅ | `Down` 0x10 |
| 5 | Zoom Tele | ✅ | `Zoom In` 0x20 |
| 6 | Zoom Wide | ✅ | `Zoom Out` 0x40 |
| 7 | Focus Near | ✅ | Byte3=0x01 |
| 8 | Focus Far | ✅ | Byte4=0x80 |
| 9 | Set Preset | ✅ | 0x03 |
| 10 | Goto Preset | ✅ | `Call Preset` 0x07 |
| 11 | Clear Preset | ✅ | 0x05 |
| 12 | Run Group | ❌ | FoMaKo 표에 없음 |
| 13 | Run Swing | ❌ | FoMaKo 표에 없음 |
| 14 | Initialize Pan/Tilt | ❌ | FoMaKo 표에 없음 |
| 15 | Aux On | ❌ | FoMaKo 표에 없음 |
| 16 | Aux Off | ❌ | FoMaKo 표에 없음 |
| 17 | Set Pan Position | ❌ | FoMaKo 표에 없음 (절대좌표 Set 자체가 없음) |
| 18 | Set Tilt Position | ❌ | 〃 |
| 19 | Set Zoom Position | ❌ | 〃 |
| 20 | Set Focus Position | ❌ | 〃 |
| 21 | Query Pan Position | ✅ | 0x51 |
| 22 | Query Tilt Position | ✅ | 0x53 |
| 23 | Query Zoom Position | ✅ | 0x55 |
| 24 | Query Focus Position | ❌ | FoMaKo 표에 없음 |
| 25 | Response Pan Position | ✅ | 0x59 |
| 26 | Response Tilt Position | ✅ | 0x5B |
| 27 | Response Zoom Position | ✅ | 0x5D |
| 28 | Response Focus Position | ❌ | FoMaKo 표에 없음 |

추가로 FoMaKo 표에는 ZU-EPC7000 표에 없던 항목도 명시되어 있다:
- **Upleft/Upright/DownLeft/DownRight** (대각선 이동, 비트 OR 조합) — 4절에서 이미 이론적으로 예상한
  조합이 실제 카메라 스펙에도 명시됨을 확인.
- **Stop** (`FF ADDR 00 00 00 00 check`, 전부 0) — 축 구분 없는 단일 정지 명령으로 명시되어 있다. 이는
  11절 체크리스트의 "Stop 명령 축 구분" 항목에 대한 직접적인 근거가 된다 (아래 참고).

**결론: 15/28행(+ 대각선 4종 + Stop 1종)이 FoMaKo에서 실제로 지원되고, 9/28행은 FoMaKo 자체 스펙에
아예 없다.** ZU-EPC7000이 이 9개 명령을 보내더라도 FoMaKo가 어떻게 반응할지는 문서상 보장이 없다 —
무시하거나, 예상 밖의 동작을 할 수 있다. 게이트웨이 구현 우선순위는 지원 확인된 15개(+대각선+Stop)에
두고, 나머지 9개는 "받으면 로그만 남기고 무시" 쪽으로 두는 게 안전하다 (10절 표에 반영).

### 7.2 FoMaKo Pelco-P 지원

FoMaKo도 Pelco-P를 자체 지원하며(5.5절), 구조는 Pelco-D와 동일한 바이트 배치를 XOR 체크섬 + `0xA0`/`0xAF`
프레임으로 감싼 형태다. 상세 내용은 [`pelcoP_command.md`](pelcoP_command.md) 참고.

### 7.3 프리셋 개수 — 체크리스트 항목 해소

FoMaKo 매뉴얼 2.6절(Technical Parameter) PTZ Parameter 표에 명시:

> **Preset Number: 255 presets (10 presets via remote control)**

즉 FoMaKo는 최대 255개 프리셋을 지원하며, 그중 IR 리모컨으로 직접 접근 가능한 건 10개(0~9번 키)뿐이고
나머지는 RS232/RS485/VISCA 같은 시리얼 제어로만 접근 가능하다. 이건 11절 체크리스트의 "FoMaKo 실제
프리셋 저장 개수" 항목을 사실상 해소한다 — VISCA `CAM_Memory`의 pq 필드 범위(0~254)와 정확히 맞아
떨어진다.

---

## 8. EDIS ED-P10N/ED-P20N (ZU-EPC7000 컨트롤러 대상) 운용 제약사항

EDIS ED-P 시리즈 사용설명서(`UMK170722-1_ED-P_시리즈_v1.0.0.pdf`)는 바이트 레벨 Pelco-D 스펙을 싣고
있지 않지만, **실제 기존에 이 카메라를 이 컨트롤러로 운용하며 지켜야 했던 제약**들을 명시하고 있어
게이트웨이 설계에 직접 영향을 준다. (주의: 이 절의 EDIS 카메라는 참고용 구(舊) 카메라이고, 실제 타겟은
FoMaKo다 — 두 카메라 스펙이 다를 수 있는 항목은 각주로 구분한다.)

### 8.1 Preset 개수와 예약 번호 — 중요

- PELCO 프로토콜: 최대 **511개** 프리셋 (1~512번 중 **95번은 제외**)
- VISCA 프로토콜: 최대 **512개** 프리셋 (1~512번 모두 사용 가능)
- **Preset 95는 OSD 메뉴 진입용으로 예약**되어 있다. PELCO 제어기의 `MENU` 버튼이 없을 때는
  `GO PRESET[95]` 또는 `RUN PATTERN[95]`로 OSD 메뉴를 연다. 즉 RS485로 `Goto Preset`(0x07, Preset
  ID=95)이 들어오면 카메라는 이걸 "95번 위치로 이동"이 아니라 **"OSD 메뉴 열기"**로 해석한다.
  - Pelco-D→VISCA 변환 로직에서 Preset ID=95는 일반 `CAM_Memory Recall`로 그대로 치환하면 안 되고
    별도 케이스로 처리해야 할 가능성이 높다 (VISCA 쪽에 동일한 "OSD 열기" 대응 명령이 있는지는
    FoMaKo 카메라 기준으로 별도 확인 필요 — VISCA 표에는 `SYS_Menu`(`8x 01 04 06 06 02/03 FF`)가
    있어 이걸로 대응 가능해 보인다).
  - 95번 프리셋은 최대 속도(Preset Max Speed) 설정도 지원하지 않는다 (같은 이유로 OSD 메뉴 진입에
    쓰이기 때문).
  - **참고: 이건 EDIS 카메라 고유의 관행이며 Pelco-D 프로토콜 자체 규칙이 아니다.** FoMaKo 매뉴얼
    (7.1절 대조 결과 포함)에는 특정 프리셋 번호를 예약한다는 언급이 전혀 없다 — FoMaKo는 IR
    리모컨에 별도 `MENU` 버튼이 있어(3.1절) 애초에 "프리셋으로 메뉴 열기" 우회가 필요 없을 가능성이
    높다. 11절 체크리스트 항목도 이에 맞춰 갱신했다.
- **VISCA ↔ PELCO 프로토콜 전환 시 저장된 프리셋 데이터가 전부 초기화**된다. 게이트웨이의 Input
  Protocol 설정(현재 RS485 Settings 메뉴의 "5. Set Input Protocol")을 운영 중 바꾸면 카메라 쪽
  프리셋이 날아갈 수 있다는 걸 사용자에게 안내할 필요가 있다.

### 8.2 카메라 ID(주소) 범위

- PELCO 프로토콜: **1~15** (0번 ID는 절대 사용 금지 — 카메라 매뉴얼이 강하게 경고) — 이건 EDIS
  카메라 자체의 제한이다.
- VISCA 프로토콜: **1~7** (0번은 컨트롤러가 연결 순서로 자동 부여하는 특수값)
- 본 프로젝트의 카메라 슬롯은 1~7 고정이므로 PELCO ID 8~15는 애초에 매핑 대상 밖 — Pelco-D 입력의
  Address가 8 이상이면 무시(ignore) 처리하면 된다.
- **컨트롤러(ZU-EPC7000) 자체는 PELCO 주소로 1~255까지 지원**한다 (9.1절). 즉 "1~15"는 EDIS 카메라
  쪽 제한이지 컨트롤러 쪽 제한이 아니다 — 게이트웨이는 컨트롤러가 이론상 1~255까지 보낼 수 있다는
  전제로, 8 이상은 어차피 이 프로젝트의 카메라 슬롯 밖이라 무시하는 현재 방침을 유지한다.

### 8.3 통신 방식 제약

- 이 카메라는 **PELCO 프로토콜을 RS-485로만** 지원한다 (VISCA는 RS-232C/RS-422만 지원, RS-485는
  VISCA에서 지원 안 됨). 즉 "RS485 입력 → Pelco-D 해석"이라는 이 프로젝트의 전제와 카메라의 실제
  결선 방식이 이 카메라 라인 한정으로는 자연스럽게 맞아떨어진다. **ZU-EPC7000 컨트롤러 자체 매뉴얼도
  동일한 제약을 명시하고 있어(9.2절) 컨트롤러·카메라 양쪽에서 교차 확인됨.**
- Baudrate는 DIP 스위치 기준 PELCO-D 옵션에서 **2400 / 9600 / 38400bps** 중 선택. (FoMaKo 카메라
  자체는 115200bps까지 지원하지만, ZU-EPC7000 컨트롤러 OSD의 Baudrate 옵션이 최대 38400bps까지라
  실질적으로 이 범위 안에서 정해진다 — 9.1절 참고.)

### 8.4 Preset Set/Goto/Clear 컨트롤러 조작 (참고용) — 삭제 절차 확인됨

RS485 바이트 자체는 5절 표(Set Preset 0x03 / Goto Preset 0x07 / Clear Preset 0x05)와 동일하며, 아래는
ZU-EPC7000류 표준 PELCO 제어기에서 그 바이트를 만들어내는 버튼 조작이다.

| 동작 | 리모컨 | 제어기(PELCO) |
|---|---|---|
| 설정 | 숫자 버튼 → SHIFT + PRESET | 숫자 버튼 → PRESET (1초 이상 길게) |
| 실행 | 숫자 버튼 → PRESET | 숫자 버튼 → PRESET (짧게) |
| 삭제 | 숫자 버튼 → SHIFT + RESET PRESET | 숫자 버튼 → IRIS[AUTO] (1초 이상 길게, MODE OFF 상태) |

- 삭제 절차는 EDIS 카메라 매뉴얼에는 명시되어 있지 않았으나, **ZU-EPC7000 컨트롤러 자체 매뉴얼**
  (p.23 PRESET 버튼 절)에서 확인했다: "숫자 버튼을 이용하여 삭제할 PRESET 번호를 누른 후, IRIS
  [AUTO] 버튼을 1초간 길게 누릅니다." 프로토콜(VISCA/PELCO)에 따라 지원하는 프리셋 개수만 다를 뿐
  조작 절차 자체는 공통으로 보인다.

### 8.5 프리셋에 저장되는 카메라 상태 (참고)

프리셋 1개 실행/설정 시 같이 저장·복원되는 항목은 세 층위로 나뉜다:

| 저장 방식 | 항목 예시 |
|---|---|
| 프리셋별 저장 (OSD에서 직접 바꿔도 저장 안 되고, 반드시 프리셋 설정을 거쳐야 저장됨) | Pan/Tilt Position, Zoom Position, Focus Mode/Position, Backlight Compensation, Exposure/White Balance/Picture 관련 다수 설정 |
| 시스템 전체 저장 (OSD에서 바꾸고 상위 메뉴로 나가면 바로 저장, 프리셋과 무관) | Home Position, Preset Speed, Power Up Action, Auto Flip, Parking, Auto Power Off 등 |

게이트웨이 입장에서는 이 구분이 직접적인 영향은 없지만, "OSD로 밝기를 바꿨는데 프리셋 실행하니
원래대로 돌아간다" 같은 현상을 디버깅할 때 참고할 수 있다.

---

## 9. ZU-EPC7000 컨트롤러 자체 사양 (컨트롤러 매뉴얼 기준)

`ZU-EPC7000+System+PTZ+Controller+manual_kor_Ver1.85.pdf`(실제 사용 중인 RS485 컨트롤러 본인의
매뉴얼)에서 확인한, 컨트롤러 동작에 관한 사실들.

### 9.1 프로토콜 지원 및 채널별 개별 설정

- p.5: "각 채널마다 개별적으로 프로토콜을 지정하여 사용 가능합니다. (VISCA, PELCO-D, PELCO-P
  프로토콜 지원)" — VISCA, PELCO-D, PELCO-P 세 가지 다 지원하며, **카메라 채널(주소)마다 각각 다른
  프로토콜을 지정**할 수 있다.
- 제어 가능 대수: VISCA는 **1~7대**, PELCO(D/P 공통)는 **1~255대**.
- 연결기기 설정 메뉴(p.28)에서 카메라 주소(CAM ADDR 1~255)마다 `PROTOCOL` 항목으로
  `PELCO-D`/`PELCO-P`/`VISCA` 중 하나를 개별 지정한다.
- Baudrate 옵션: 2400 / 4800 / 9600 / 19200 / 38400bps (컨트롤러 OSD 기준, 115200은 옵션에 없음).

### 9.2 통신 방식 ↔ 프로토콜 매핑

- **VISCA → RS-422 또는 RS-232C** (RS-485 아님)
- **PELCO-D/P → RS-485만** (프로토콜을 PELCO로 설정하면 자동으로 RS-485로 전환됨, p.23)
- VISCA 사용 시 RS-422/RS-232C 전환은 `IRIS[MANUAL]` 길게 눌러 토글, RS-232C LED로 현재 모드 표시.

### 9.3 PELCO-D ↔ PELCO-P 프로토콜 혼재 가능성 — 구현 완료

이전에 "RS485 입력에 VISCA와 Pelco-D가 섞여서 올 수 있는가"를 논의했을 때(코드에는 반영하지 않기로
함), `0xFF` 바이트가 VISCA 종료 바이트와 Pelco-D SYNC/데이터/체크섬 바이트에서 겹쳐서 안전한 실시간
구분이 어렵다는 결론을 냈었다. 이 컨트롤러 매뉴얼을 보고 나서 그 논의가 더 명확해졌고, 이후
실제로 코드에 반영했다:

- **VISCA ↔ PELCO 계열은 물리적으로 섞일 수 없다.** VISCA는 RS-422/RS-232C 커넥터로, PELCO는
  RS-485 커넥터로 나가는 경로 자체가 분리되어 있어서, 같은 RS-485 두 가닥에 VISCA 프레임이 실릴
  수가 없다. 즉 `0xFF` 겹침으로 인한 오판독 리스크는 VISCA-Pelco 조합에서는 애초에 발생하지 않는다.
  이 때문에 Input Protocol의 자동 판별 옵션은 VISCA를 포함하지 않는다 — VISCA는 항상 명시적으로
  선택해야 한다.
- **다만 Pelco-D ↔ Pelco-P는 같은 RS-485 버스에서 진짜로 섞일 수 있다.** 카메라 주소별로 프로토콜이
  개별 저장되므로(9.1절), 예를 들어 CAM 3은 PELCO-D로, CAM 5는 PELCO-P로 설정돼 있으면 같은
  RS-485 라인에 두 프로토콜이 실제로 번갈아 나갈 수 있다.
  - 다행히 Pelco-D와 Pelco-P는 시작 바이트가 다르다 (Pelco-D는 `0xFF`, Pelco-P는 `0xA0` —
    `pelcoP_command.md` 2절 참고). VISCA/Pelco-D 조합처럼 `0xFF` 겹침 문제가 없어서, 시작 바이트만
    보고도 비교적 안전하게 두 프로토콜을 구분할 수 있다.
- **구현 완료 (2026-08-02):** RS485 Settings의 Input Protocol에 `VISCA` / `Pelco-D` / `Pelco-P` /
  `Pelco-D/P Autodetect` 네 가지 옵션이 추가되었다. `Pelco-D/P Autodetect`는 패킷이 시작될 때(두
  파서가 모두 유휴 상태일 때)만 시작 바이트를 보고 어느 파서로 넘길지 정하고, 프레임이 진행 중인
  동안에는 그 파서에만 바이트를 계속 먹인다 — 진행 중인 프레임의 페이로드 바이트가 우연히 상대
  프로토콜의 시작 바이트와 같아서 유휴 파서가 잘못 새 프레임을 시작하는 오탐(false start)을 막기
  위함이다 (`src/main.cpp`의 `feedPelcoAutoByte()`). Pelco-D/Pelco-P 모두 프레이밍/체크섬 검증과
  General Response(ACK) 회신까지만 구현되어 있고, VISCA로의 실제 명령 변환은 아직 없다 (기존
  Pelco-D와 동일한 범위). ACK 응답 여부는 두 프로토콜 공통 설정인 `PelcoResponseMode`
  ("6. Set Pelco Response Mode")로 제어한다.

### 9.4 Preset 삭제 절차 — 8.4절에 반영 완료

번호 입력 → `IRIS[AUTO]` 1초 길게 → 삭제. 8.4절 표에 반영함.

---

## 10. Preset → VISCA 매핑

Pelco-D Set/Goto/Clear Preset은 지금까지 다룬 것 중 가장 단순하게 1:1 매핑된다 — Pan/Tilt 속도처럼
범위를 재계산하거나 Zoom Position처럼 니블 패킹할 필요 없이, **Preset ID 바이트를 그대로 복사**하면 된다.

| Pelco-D | VISCA |
|---|---|
| Set Preset (`FF ADDR 00 03 00 [ID] check`) | `CAM_Memory` Set — `8x 01 04 3F 01 [pq] FF` |
| Goto/Call Preset (`FF ADDR 00 07 00 [ID] check`) | `CAM_Memory` Recall — `8x 01 04 3F 02 [pq] FF` |
| Clear Preset (`FF ADDR 00 05 00 [ID] check`) | `CAM_Memory` Reset(=삭제) — `8x 01 04 3F 00 [pq] FF` |

- 둘 다 Preset ID / Memory Number가 **단일 바이트**(pq = 0~254)라서 변환 로직이 가장 단순함.
- 의미상으로도 정확히 대응: 둘 다 "현재 Pan/Tilt/Zoom/Focus 상태를 번호 슬롯에 저장/불러오기/삭제".
- FoMaKo 자체가 Set/Goto/Clear Preset을 전부 지원함을 7.1절에서 확인했다 — 세 명령 모두
  `translatePelcoAndForward()`에 구현 완료.

### 다른 기능들과 비교 (FoMaKo 자체 지원 여부 반영)

| 기능 | VISCA 매핑 | FoMaKo 자체 Pelco-D 지원(7.1절) |
|---|---|---|
| Pan/Tilt/Zoom/Focus 이동 (+대각선, Stop) | 가능 (속도 값 재스케일 필요) | ✅ 지원 |
| **Preset Set/Goto/Clear** | **가능 (그대로 복사, 재계산 불필요)** | ✅ 지원 |
| Query 절대좌표(Pan/Tilt/Zoom Position, 읽기 전용) | 가능 (`Pan-tiltPosInq`/`CAM_ZoomPosInq`) | ✅ 지원 |
| Set 절대좌표(Pan/Tilt/Zoom Position, 쓰기) | VISCA `AbsolutePosition`/`CAM_Zoom Direct`로 대체 가능 | ❌ FoMaKo Pelco-D 표에 없음 — Pelco-D 경유로는 불가, VISCA 쪽 별도 명령으로 우회해야 함 |
| Query/Response Focus Position | VISCA `CAM_FocusPosInq` 있음 | ❌ FoMaKo Pelco-D 표에 없음 |
| Run Group / Run Swing | 불가 — VISCA에 대응 명령 없음 | ❌ FoMaKo Pelco-D 표에도 없음 (이중으로 불가) |
| Aux On/Off | 불가 — VISCA에 대응 명령 없음 | ❌ FoMaKo Pelco-D 표에도 없음 (이중으로 불가) |
| Initialize Pan/Tilt | VISCA `CAM_SettingReset` 정도로 대체 가능하나 의미가 다름 | ❌ FoMaKo Pelco-D 표에 없음 |

**우선순위 결론 → 구현 완료 (2026-08-02):** 이동(대각선 포함)/Stop/Preset Set·Goto·Clear — 이
6종은 FoMaKo가 Pelco-D 자체로 지원하고 VISCA 매핑도 명확해서 `src/main.cpp`의
`translatePelcoAndForward()`로 구현했다. 절대좌표 Query(Pan/Tilt/Zoom Position)는 VISCA 쪽
조회가 비동기 응답(별도 UDP 패킷)으로 오는 구조라 Pelco Extended Response로 재포장하는 로직이
추가로 필요해서 이번 구현에는 포함하지 않았다 — 요청을 받으면 조용히 무시한다(후속 작업).
절대좌표 Set과 Focus Position Query/Response는 Pelco-D 입력만으로는 FoMaKo가 못 받는 명령이라,
ZU-EPC7000이 실제로 이 바이트를 보내는지부터 실측 확인이 필요하다 (11절에 반영). Run
Group/Swing/Aux/Initialize는 양쪽 모두 근거가 없어 구현 대상에서 제외했다.

번역 로직 요약(`src/main.cpp`):
- CMND1==0x00 && CMND2가 `0x03`/`0x05`/`0x07`(Preset Set/Clear/Goto)인 경우를 먼저 확인한다 —
  Standard Command 비트 플래그는 CMND2 bit0이 항상 0으로 정의되어 있어 이 홀수 옵코드들과
  절대 겹치지 않는다(4절).
- CMND1==0x00 && CMND2==0x00이면 Stop — VISCA Pan/Tilt Stop + Zoom Stop + Focus Stop 3개를
  전부 보낸다.
- 그 외에는 Standard Command 비트 플래그로 해석해 Pan/Tilt(대각선 조합 포함)/Zoom/Focus를
  각각 독립적으로 판단하고, 동시에 여러 축이 세팅돼 있으면 VISCA 명령을 여러 개 보낸다(예:
  이동+줌이 한 패킷에 같이 온 경우).
- Pan/Tilt 속도(DATA1/DATA2)는 `scalePelcoSpeedToVisca()`가 0x00~0x3F 가정으로 VV(0x01~0x18)/
  WW(0x01~0x14)로 환산한다 — 11절의 "우선 처리" 정책을 그대로 코드화한 것.
- Pelco-D는 ADDR을 그대로 카메라 번호로 쓰고, Pelco-P는 ADDR이 "실제 주소 - 1"이라 +1 보정
  후 사용한다(`pelcoP_command.md` 1/7절). 두 경우 다 카메라 슬롯 1~7 밖이면 무시한다.

## 11. 확인이 필요한 사항 (실측 전까지 보류)

아래는 문서만으로는 확정할 수 없고, 실제 ZU-EPC7000 컨트롤러 + FoMaKo 카메라를 연결해서
**Live Packet Monitor**(Debug Mode → 4. Live Packet Monitor)로 실측 확인이 필요한 항목들이다.

- [x] **FoMaKo의 실제 프리셋 저장 개수.** ~~VISCA `CAM_Memory`의 pq 필드는 스펙상 0~254까지 허용하지만,
      FoMaKo가 실제로 몇 개까지 물리적으로 저장하는지는 문서에 없음.~~
  - **확인 완료 (2026-08-02):** FoMaKo 매뉴얼 2.6절에 "Preset Number: 255 presets (10 presets via
    remote control)"로 명시되어 있음. 최대 255개, VISCA `CAM_Memory`의 pq 범위(0~254)와 정확히
    맞아떨어진다. 상한선을 임의로 제한할 필요 없이 pq 전체 범위를 그대로 통과시키면 된다 (기존
    "우선 처리" 방침 그대로 유지, 다만 이제는 추측이 아니라 확인된 사실).
- [ ] **ZU-EPC7000이 실제로 Preset 95를 OSD 메뉴 진입 용도로 보내는지.** 이건 EDIS 카메라(또는 MENU
      버튼이 없는 컨트롤러)의 관행이지 Pelco-D 프로토콜 자체 규칙이 아님. FoMaKo 매뉴얼(7.1/8.1절)에는
      특정 프리셋 번호를 예약한다는 언급이 전혀 없고, FoMaKo IR 리모컨에는 별도의 물리 `MENU` 버튼이
      있어(3.1절) 애초에 "프리셋으로 메뉴 열기" 우회가 필요 없을 가능성이 높아졌다. 다만 ZU-EPC7000
      컨트롤러가 실제로 어떤 바이트를 보내는지는 여전히 실측이 필요함 — 컨트롤러의 MENU 버튼을
      눌렀을 때 어떤 RS485 바이트가 나가는지 확인.
  - **우선 처리:** 특별 케이스를 만들지 않는다. 95번도 그냥 일반 Preset Recall로 VISCA에 그대로
    전달한다. FoMaKo 쪽에 95번 예약 관행이 없다는 정황(위 참고)이 이 방침을 더 뒷받침한다.
- [ ] **ZU-EPC7000이 명령마다 응답(ACK)을 기다리는지.** FUJIFILM SX800/801은 모든 명령에 4바이트
      General Response를 요구하지만, ZU-EPC7000 원본 표에는 이런 요구사항이 없었음. 컨트롤러가 응답
      없이도 다음 명령을 계속 보내는지, 아니면 응답을 기다리다 타임아웃되는지 확인 — 필요하면 VISCA
      쪽처럼 `Response Mode: synthetic` 개념을 Pelco-D 쪽에도 추가해야 할 수 있음.
  - **구현 완료 (2026-08-01):** 기본값을 "응답 보냄"으로 구현했다. 리스크가 비대칭적이기 때문 —
    응답이 불필요한데 보내면 컨트롤러가 그냥 무시하는 정도로 끝나지만(RS485 half-duplex라 응답을
    내보내는 짧은 순간만 버스를 점유하고 바로 수신 모드로 복귀하므로 부작용이 적음), 반대로 응답이
    필요한데 안 보내면 컨트롤러가 첫 명령 이후로 다음 명령을 안 보내고 멈춰버려 게이트웨이가
    사실상 통째로 동작을 안 하는 셈이 된다. RS485 Settings 메뉴에 "6. Set Pelco Response Mode"로
    노출되며, 유효한 Pelco-D 패킷(체크섬 통과)을 처리할 때마다 General Response
    (`FF ADDR 00 CKSM`, 4바이트, CKSM은 원본 패킷의 체크섬 바이트를 그대로 사용)를 합성해서
    돌려준다. `1. Respond (synthetic ACK)`(기본값) / `2. No response` 중 선택 가능. 실측해서
    컨트롤러가 응답을 기대하지 않는 게 확인되면 `2`로 바꾸면 된다.
    (**2026-08-02 갱신:** 이 설정 이름을 `PelcoResponseMode`로 일반화하고 Pelco-P에도 동일하게
    적용했다 — `pelcoP_command.md` 참고. ZU-EPC7000 컨트롤러의 "ACK MODE" 설정이 Pelco-D/P를
    구분하지 않는 것과 일관된 설계.)
- [ ] **Pan/Tilt Speed(DATA1/DATA2)의 실제 값 범위.** ZU-EPC7000이 어느 범위(0x00~0x3F인지,
      0xFF 터보 포함인지 등)로 속도를 보내는지 실측하여 VISCA `VV`(0x01~0x18)/`WW`(0x01~0x14)로
      스케일링하는 공식을 확정.
  - **우선 처리:** 확인 전까지는 Pelco-D 표준 관례인 0x00~0x3F(6bit, 0~63)를 입력 범위로 가정하고
    `VV = round(입력 * 0x18 / 0x3F)` 같은 선형 비례식으로 VISCA 속도로 환산한다. 0을 포함해 0으로
    나누는 경우나 0x3F를 넘는 값(터보 등)은 각각 최소/최대값으로 clamp. 실측치가 다르면 이 공식만
    교체하면 되도록 별도 함수로 분리해서 구현.
- [x] **Stop 명령 시 Pan/Tilt/Zoom/Focus 중 무엇을 멈춰야 하는지 구분 가능 여부.** ~~Pelco-D Stop은
      Byte3/Byte4가 전부 0인 패킷 하나뿐이라 어떤 축이 멈춰야 하는지 패킷만으로는 알 수 없음~~
  - **확인 완료 (2026-08-02):** FoMaKo 자체 Pelco-D 표(7.1절)에도 `Stop`이 축 구분 없는 단일 명령
    (`FF ADDR 00 00 00 00 check`)으로 명시되어 있다 — 별도의 "Zoom Stop"/"Focus Stop" 행이 없다.
    즉 FoMaKo 자신도 Pelco-D 레벨에서는 Stop을 전체 정지로 취급한다는 뜻이므로, 게이트웨이가 Pelco-D
    Stop 수신 시 VISCA Pan/Tilt Stop + Zoom Stop + Focus Stop을 한꺼번에 보내는 기존 계획이
    FoMaKo의 실제 동작 방식과 의미상 일치한다. 계획대로 확정.
