# Pelco-P Command List

**구현 완료 (2026-08-02):** RS485 Settings 메뉴의 Input Protocol에서 `3. Pelco-P` 또는
`4. Pelco-D/P Autodetect`를 선택하면 게이트웨이가 이 문서의 프레이밍/체크섬을 사용해 Pelco-P
패킷을 인식한다 (`src/PelcoPParser.h/.cpp`). ZU-EPC7000 컨트롤러가 카메라 채널마다 프로토콜을
개별 지정할 수 있어([`pelcoD_command.md`](pelcoD_command.md) 9.1/9.3절) 실제로 Pelco-D/Pelco-P가
같은 RS485 버스에 섞여 들어올 수 있다는 점이 구현 계기였다.

**구현 완료 (2026-08-02):** VISCA로의 실제 명령 변환도 Pelco-D와 공유하는
`translatePelcoAndForward()`(`src/main.cpp`)로 구현했다. 4절의 CMND1 비트 재배치(Focus Far/Near
위치)만 Pelco-D와 다르게 분기하고, 나머지(Pan/Tilt/Zoom 비트, Preset 옵코드)는 5절에서 확인한
대로 Pelco-D와 완전히 동일한 값 체계를 그대로 공유한다. 주소는 7절 표의 "실제 주소 - 1"
인코딩을 반영해 +1 보정 후 카메라 슬롯 1~7에 매핑한다 (`handlePelcoPPacket()`). Query Pan/Tilt/
Zoom Position은 Pelco-D와 마찬가지로 아직 번역하지 않는다 — VISCA 조회는 비동기 응답(별도 UDP
패킷)으로 오는데, 이걸 Pelco Extended Response로 재포장하는 로직이 없어서 요청만 받고 무시한다.
아래 5절의 커맨드 목록은 이 번역 로직의 근거 자료다.

FUJIFILM SX1600용 공식 Pelco-P 프로토콜 스펙(`pelco-p_protocol_specification_for_sx1600_v.1.00.0_en.pdf`)과
실제 타겟 카메라인 FoMaKo의 자체 Pelco-P 커맨드 표(`fomako_manual.pdf` 5.5절)를 교차 참조해 정리했다.
컨트롤러(ZU-EPC7000)의 Pelco-P 지원 여부는 `ZU-EPC7000+System+PTZ+Controller+manual_kor_Ver1.85.pdf`로
확인했다.

---

## 1. 프로토콜 개요

- Master-Slave 구조. 최대 32개 슬레이브를 하나의 마스터에 연결 가능 (FUJIFILM 스펙 기준).
- 주소 범위: **0~30** (31개, `RS485_ID`는 1~31) — **주소 바이트는 "장치에 설정된 실제 주소 - 1"을
  사용한다.** Pelco-D처럼 실제 주소를 그대로 쓰지 않고 1을 뺀 값을 ADDR 필드에 넣는다는 점이 큰
  차이이며, 구현 시 실수하기 쉬운 지점이다.
- Baudrate: 2400 / 4800 / 9600 / 19200 / 38400 / 115200 (FUJIFILM SX1600 기준. 공장 출하 기본값은
  9600).
- 시리얼 포맷: Start bit 1, Data 8bit, Stop bit 1, Parity None (Pelco-D와 동일).
- Pelco 표준 규정: Standard Command로 구동되는 모든 동작은 최대 15초 후 자동 정지된다(런어웨이 방지).
  타임아웃 전에 같은 구동 명령이 다시 오면 타이머가 리셋된다.

## 2. Send Command 포맷

| Byte | 1    | 2    | 3     | 4     | 5     | 6     | 7    | 8    |
| ---- | ---- | ---- | ----- | ----- | ----- | ----- | ---- | ---- |
| 필드 | STX  | ADDR | CMND1 | CMND2 | DATA1 | DATA2 | ETX  | CKSM |
| 값   | 0xA0 | -    | -     | -     | -     | -     | 0xAF | -    |

**Pelco-D와의 핵심 차이:**
- 시작 바이트가 `0xFF`가 아니라 **`0xA0`**. 프레임 길이도 7바이트가 아니라 **8바이트**(ETX 고정 바이트
  `0xAF`가 추가로 들어감).
- 체크섬이 합산이 아니라 **Byte2~Byte6의 XOR** (8bit).
- 이 두 가지(시작 바이트, 프레임 길이) 덕분에 같은 RS-485 버스에 Pelco-D와 Pelco-P가 섞여 있어도
  실시간으로 구분하기가 Pelco-D/VISCA 조합보다 훨씬 안전하다 — VISCA는 `0xFF`가 종료 바이트라서
  Pelco-D의 시작 바이트와 겹치는 문제가 있었지만, Pelco-P의 `0xA0`은 Pelco-D 프레임의 어떤 필드와도
  구조적으로 혼동될 일이 적다 ([`pelcoD_command.md`](pelcoD_command.md) 9.3절 참고, 코드 미반영).

## 3. Receive Command 포맷 (응답)

FUJIFILM SX1600 스펙 기준. Pelco-D와 마찬가지로 General/Extended/Query 세 종류가 있다.

- **General Response** (5 byte): `SYNC(0xA0) ADDR ALARMS(0x00) ETX(0xAF) CKSM` — CKSM은 수신한
  CKSM과 ALARMS의 XOR.
  - 참고: FUJIFILM 스펙 원문의 5.1.2절(Standard Command 응답) 표에는 STX가 `0xFF`로 오기되어 있고
    바로 아래 설명 문구는 "Always set 0xA0 to STX"라고 되어 있어 스펙 자체에 모순이 있다 — Pelco-D
    스펙 문서를 복사해서 만들다 생긴 오타로 보인다. 3.2.1절(개요 챕터)의 정의(`0xA0`)를 신뢰한다.
- **Extended Response** (8 byte): `STX(0xA0) ADDR RESP1 RESP2 DATA1 DATA2 ETX(0xAF) CKSM` — 받은
  CMND1/CMND2를 그대로 RESP1/RESP2에 되돌려주고, DATA1/DATA2에 조회 결과를 담는다. CKSM은
  Byte2~Byte8의 XOR.
- **Query Response** (19 byte): `STX(0xA0) ADDR DATA1..DATA15 ETX(0xAF) CKSM` — Serial Number 조회처럼
  데이터가 긴 경우.

## 4. Standard Command (비트 플래그)

Pelco-D와 마찬가지로 비트 플래그 방식이지만, **비트 배치가 한 자리씩 밀려 있다** — Pelco-D는 Sense
비트(bit7)를 썼지만 Pelco-P는 그 자리가 없고, 대신 Focus Far가 CMND2에서 CMND1으로 옮겨왔다.

**Byte3, CMND1**

| Bit7 | Bit6      | Bit5        | Bit4          | Bit3       | Bit2      | Bit1       | Bit0      |
| ---- | --------- | ----------- | ------------- | ---------- | --------- | ---------- | --------- |
| 0    | Camera On | Autoscan On | Camera On/Off | Iris Close | Iris Open | Focus Near | Focus Far |

**Byte4, CMND2**

| Bit7 | Bit6      | Bit5      | Bit4 | Bit3 | Bit2 | Bit1  | Bit0    |
| ---- | --------- | --------- | ---- | ---- | ---- | ----- | ------- |
| 0    | Zoom Wide | Zoom Tele | Down | Up   | Left | Right | 항상 0 |

- FUJIFILM 스펙 원문: "Bit4~Bit7 of CMND1은 최신 Pelco-P에서 쓰이지 않으므로 이 스펙도 지원하지
  않는다"고 명시 — 즉 실제로 카메라가 반응하는 건 CMND1의 Bit0~Bit3(Focus Far/Near, Iris
  Open/Close)뿐일 수 있다. Camera On/Off, Autoscan은 카메라 펌웨어에 따라 지원 여부가 갈릴 수 있음
  (Pelco-D의 FUJIFILM SX800/801 사례와 동일한 패턴).
- Focus Far/Near가 Pelco-D에서는 각각 CMND2 bit7 / CMND1 bit0에 흩어져 있었는데, Pelco-P에서는 둘 다
  CMND1(bit0/bit1)로 모여 있다 — Pelco-D 파서 로직을 재사용하려면 이 차이를 반드시 반영해야 한다.

## 5. FoMaKo 카메라(실제 타겟) 자체 Pelco-P 커맨드 목록

`fomako_manual.pdf` 5.5절. Address(Byte2)는 예시로 표기.

| Function | Byte1 | Byte2 | Byte3 | Byte4 | Byte5 | Byte6 | Byte7 | Byte8 |
|---|---|---|---|---|---|---|---|---|
| Up | 0xA0 | Address | 0x00 | 0x08 | Pan Speed | Tilt Speed | 0xAF | XOR |
| Down | 0xA0 | Address | 0x00 | 0x10 | Pan Speed | Tilt Speed | 0xAF | XOR |
| Left | 0xA0 | Address | 0x00 | 0x04 | Pan Speed | Tilt Speed | 0xAF | XOR |
| Right | 0xA0 | Address | 0x00 | 0x02 | Pan Speed | Tilt Speed | 0xAF | XOR |
| Upleft | 0xA0 | Address | 0x00 | 0x0C | Pan Speed | Tilt Speed | 0xAF | XOR |
| Upright | 0xA0 | Address | 0x00 | 0x0A | Pan Speed | Tilt Speed | 0xAF | XOR |
| DownLeft | 0xA0 | Address | 0x00 | 0x14 | Pan Speed | Tilt Speed | 0xAF | XOR |
| DownRight | 0xA0 | Address | 0x00 | 0x12 | Pan Speed | Tilt Speed | 0xAF | XOR |
| Zoom In | 0xA0 | Address | 0x00 | 0x20 | 0x00 | 0x00 | 0xAF | XOR |
| Zoom Out | 0xA0 | Address | 0x00 | 0x40 | 0x00 | 0x00 | 0xAF | XOR |
| Stop | 0xA0 | Address | 0x00 | 0x00 | 0x00 | 0x00 | 0xAF | XOR |
| Focus Far | 0xA0 | Address | 0x01 | 0x00 | 0x00 | 0x00 | 0xAF | XOR |
| Focus Near | 0xA0 | Address | 0x02 | 0x00 | 0x00 | 0x00 | 0xAF | XOR |
| Set Preset | 0xA0 | Address | 0x00 | 0x03 | 0x00 | Preset ID | 0xAF | XOR |
| Clear Preset | 0xA0 | Address | 0x00 | 0x05 | 0x00 | Preset ID | 0xAF | XOR |
| Call Preset | 0xA0 | Address | 0x00 | 0x07 | 0x00 | Preset ID | 0xAF | XOR |
| Query Pan Position | 0xA0 | Address | 0x00 | 0x51 | 0x00 | 0x00 | 0xAF | XOR |
| Query Pan Position Response | 0xA0 | Address | 0x00 | 0x59 | Value High | Value Low | 0xAF | XOR |
| Query Tilt Position | 0xA0 | Address | 0x00 | 0x53 | 0x00 | 0x00 | 0xAF | XOR |
| Query Tilt Position Response | 0xA0 | Address | 0x00 | 0x5B | Value High | Value Low | 0xAF | XOR |
| Query Zoom Position | 0xA0 | Address | 0x00 | 0x55 | 0x00 | 0x00 | 0xAF | XOR |
| Query Zoom Position Response | 0xA0 | Address | 0x00 | 0x5D | Value High | Value Low | 0xAF | XOR |

- **CMND2 값(Byte4)이 Pelco-D 표와 완전히 동일**하다 (Up=0x08, Down=0x10, Left=0x04, Right=0x02,
  Zoom In=0x20, Zoom Out=0x40, Preset Set/Clear/Call=0x03/0x05/0x07, Query Pan/Tilt/Zoom=0x51/0x53/0x55,
  Response=0x59/0x5B/0x5D). 4절에서 설명한 비트 배치 차이는 CMND1(Focus)에만 있고, CMND2(Pan/Tilt/
  Zoom/Preset)는 Pelco-D와 사실상 동일한 값 체계를 쓴다는 뜻이다.
- Focus Far/Near는 CMND1 값이 각각 `0x01`/`0x02` — 4절 비트 표(Bit0=Focus Far, Bit1=Focus Near)와
  일치.
- Pelco-D 쪽 7.1절 대조와 동일한 패턴으로, Run Group/Swing/Aux On-Off/절대좌표 Set/Focus Position
  Query는 FoMaKo Pelco-P 표에도 없다 — Pelco-D와 지원 범위가 사실상 동일하다.
- Preset 지원 범위(255개, 10개는 리모컨 직접 접근)도 프로토콜에 무관하게 동일할 것으로 추정되나,
  `pelcoD_command.md` 8.1절의 "VISCA↔PELCO 전환 시 프리셋 초기화" 제약이 Pelco-D↔Pelco-P 전환에도
  적용되는지는 EDIS 매뉴얼에 명시되어 있지 않아 확인 필요.

## 6. ZU-EPC7000 컨트롤러의 Pelco-P 지원

[`pelcoD_command.md`](pelcoD_command.md) 9절 참고. 요약:

- ZU-EPC7000은 VISCA/Pelco-D/Pelco-P 세 프로토콜을 모두 지원하며, **카메라 채널(주소)마다 개별
  지정**한다.
- Pelco-P도 Pelco-D와 마찬가지로 **RS-485로만** 나간다 — 프로토콜을 PELCO-P로 설정하면 자동으로
  RS-485 통신 방식이 적용된다.
- 같은 RS-485 버스에서 일부 채널은 Pelco-D로, 일부는 Pelco-P로 설정되어 있으면 두 프로토콜이 실제로
  섞여서 들어올 수 있다 (2절에서 설명한 시작 바이트 차이로 구분 가능).

## 7. Pelco-D와의 구조적 차이 요약

| 항목 | Pelco-D | Pelco-P |
|---|---|---|
| 시작 바이트 | `0xFF` | `0xA0` |
| 프레임 길이 (Standard Command) | 7 byte | 8 byte (`0xAF` 종료 바이트 추가) |
| 체크섬 | Byte2~6 합산 | Byte2~6 XOR |
| 주소 인코딩 | 실제 주소 그대로 | 실제 주소 **- 1** |
| 주소 범위 | 벤더마다 다름 (FUJIFILM 1~31, FoMaKo 1~15) | 0~30 (실제 주소 1~31) |
| CMND2 (Pan/Tilt/Zoom/Preset) | — | Pelco-D와 동일한 비트/값 배치 |
| CMND1 (Focus/Iris/Camera On) | Sense bit(bit7) + bit0=Focus Near | Sense bit 없음, bit0=Focus Far/bit1=Focus Near로 재배치 |

이 표가 근거가 되어 `PelcoDParser`를 재사용하지 않고 `src/PelcoPParser.h/.cpp`를 별도로 구현했다
(시작 바이트, 프레임 길이, 체크섬 계산이 전부 다르기 때문). CMND1 비트 재배치(Focus Far/Near
위치, 4절)는 프레이밍 레벨에서는 영향이 없지만, VISCA 변환 로직(`translatePelcoAndForward()`)의
`isPelcoP` 분기에 반영되어 있다.

## 8. 확인이 필요한 사항

- [x] ~~ZU-EPC7000의 실제 운용 환경에서 Pelco-P로 설정된 카메라 채널이 있는지.~~ 있는지 여부와
      무관하게 컨트롤러 매뉴얼상 채널별 개별 설정이 가능하다는 사실 자체가 대비할 근거로 충분하다고
      판단해 **구현 완료 (2026-08-02)**: Input Protocol에 `Pelco-P`, `Pelco-D/P Autodetect` 옵션 추가.
      실제로 Pelco-P 채널이 있는지는 여전히 Live Packet Monitor로 실측하면 좋지만, 구현이 그 확인을
      막고 있진 않다.
- [ ] FUJIFILM 스펙 5.1.2절의 STX 오기(`0xFF` vs `0xA0`) 관련해서, 실제 카메라들이 Receive Command의
      STX로 어떤 값을 실제로 내보내는지 — 스펙 오타인지 실제 동작 차이인지는 실측 전까지 알 수 없음.
      (게이트웨이의 `sendPelcoPResponse()`는 3절 개요 챕터의 정의를 따라 `0xA0`을 사용한다.)
- [x] ~~Pelco-D와 마찬가지로 Pelco-P → VISCA 명령 변환 로직 자체가 아직 없음.~~ **구현 완료
      (2026-08-02)**: `translatePelcoAndForward()`가 4절의 CMND1 비트 재배치와 7절의 주소
      인코딩(`실제 주소 - 1`) 차이를 반영해서 처리한다. Query Pan/Tilt/Zoom Position은 Pelco-D와
      동일하게 아직 번역하지 않는다(비동기 VISCA 응답을 Pelco Extended Response로 재포장하는
      로직이 필요 — 후속 작업).
