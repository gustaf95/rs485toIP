# 게이트웨이 외부 제어 인터페이스

다른 프로그램(스크립트, 쇼 컨트롤 도구, 자체 제작 앱)이 **컨트롤러 쪽 게이트웨이**를 통해
카메라를 제어하고 상태를 읽기 위한 규격이다. 브라우저 제어 패널(`/control`)이 쓰는 것과
**같은 HTTP API**이므로, 패널에서 되는 것은 전부 여기서도 되고 결과도 같다.

- 이 문서는 "어떻게 부르는가"만 다룬다. 왜 HTTP를 택했는지, 검토했다 접은 대안(Pelco-D
  over UDP, 텍스트 프로토콜, VISCA over IP)은 [cli_interface.md](cli_interface.md)에 있다.
- PC에서 바로 쓸 수 있는 래퍼 CLI는 [`tools/ptz.py`](../tools/ptz.py)다. 직접 HTTP를
  부르기 전에 그것으로 충분한지 먼저 본다.
- 규격의 실제 출처는 코드다: 명령은 `main.cpp`의 `webExecuteCommand()`, 상태는
  `webStateJson()`, 접근 판정은 `WebControl.cpp`의 `controlAllowed()`.

---

## 1. 접속

| 항목 | 값 |
| --- | --- |
| 프로토콜 | HTTP/1.1, 평문 (TLS 없음) |
| 포트 | **80** (`WEB_SERVER_PORT`) |
| 주소 | 게이트웨이의 **STA 주소** (평소 Wi-Fi에서 받은 IP, 예: `192.168.41.10`) |
| 인증 | 없음 |
| 문자 인코딩 | UTF-8. 응답 JSON은 ASCII만 쓴다 |

**STA 주소로 접속해야 제어된다.** 게이트웨이는 설정용 AP(`RS485Gateway-XXXX`,
`192.168.4.1`)도 함께 띄우는데, AP로 들어온 요청은 상태 조회만 되고 명령은 `403`으로
거절된다. 권한 경계는 "평소 LAN에 붙어 있는가" 하나뿐이다 - 같은 LAN의 누구나 제어할 수
있다는 뜻이기도 하다.

**주소를 고정한다.** 기본이 DHCP라 공유기가 재부팅되면 IP가 바뀌고, 외부 프로그램은
조용히 실패한다. 게이트웨이 설정의 Static IP를 쓰거나 공유기에서 MAC 예약을 건다. STA
주소는 설정 화면 Status의 `ESP32 IP (STA)`나 `GET /api/state`의 `sta`에서 확인한다.

---

## 2. 명령 — `POST /api/cmd`

### 2.1 요청

| 항목 | 값 |
| --- | --- |
| 메서드 / 경로 | `POST /api/cmd` |
| 본문 형식 | `application/x-www-form-urlencoded` (쿼리 문자열로 붙여도 받는다) |
| 필드 | `cam`, `action`, `p1`, `p2` |

- `cam`: 카메라 번호 **1..7**.
- `action`: 3절 표의 이름. 대소문자를 구분한다.
- `p1`, `p2`: 정수. **빠지면 0으로 읽힌다** (`String::toInt()`). 숫자가 아니어도 0이다.

```sh
curl -s -X POST http://192.168.41.10/api/cmd -d cam=6 -d action=preset_goto -d p1=3
```

### 2.2 응답

| 상황 | HTTP 상태 | 본문 |
| --- | --- | --- |
| 성공 | `200` | `{"ok":true}` |
| 게이트웨이가 거절 | **`200`** | `{"ok":false,"err":"<사유>"}` |
| AP로 접속함 | `403` | `{"ok":false,"err":"control is blocked on the AP - use the STA address"}` |

**거절도 HTTP 200으로 온다.** 성공 여부는 반드시 본문의 `ok`로 판정한다. `err`에 나올 수
있는 문장:

| `err` | 원인 |
| --- | --- |
| `camera out of range` | `cam`이 1..7 밖 |
| `preset out of range (1-255)` | 프리셋 번호가 1..255 밖 |
| `unknown action` | 3절 표에 없는 `action` |
| `RS485 control needs Pelco-D input (Pelco-P master frames not implemented)` | RS485 카메라를 불렀는데 게이트웨이의 Input Protocol이 Pelco-D/Autodetect가 아님 |

### 2.3 `ok:true`가 보장하는 것

`ok:true`는 **"게이트웨이가 명령을 내보냈다(또는 줄 세웠다)"** 이지 "카메라가 실행했다"가
아니다.

- **IP 카메라**(6번 FoMaKo 등): UDP로 보냈다는 뜻이다. 카메라가 받았는지는 알 수 없다.
- **RS485 카메라**(1·2·3번 ED-P, 터널 슬롯 4번): 송신 큐에 넣었다는 뜻이다. 버스가 30ms
  이상 조용할 때만 실제로 나가고, 500ms 안에 그 틈을 못 잡으면 **버려진다**. 버려진 개수는
  `/api/state`의 `txDropped`로만 보인다(4절).

결과를 확인해야 하면 `/api/state`의 해당 항목이 바뀌었는지 본다. 단, RS485 카메라의 상태는
제약이 있다(4.3절).

---

## 3. 명령 어휘

`cam`이 가리키는 슬롯에 IP가 설정돼 있으면 **IP 경로**(VISCA over IP), 아니면 **RS485
경로**(버스에 Pelco-D 마스터 프레임, 터널 슬롯이면 터널로)로 나간다. 호출자는 경로를 고를
필요가 없다 - 같은 `action`이 경로에 맞는 프레임으로 바뀐다. 어느 경로인지는
`/api/state`의 `cams[].path`로 알 수 있다.

| action | p1 | p2 | 동작 | 종류 |
| --- | --- | --- | --- | --- |
| `stop` | — | — | 팬/틸트·줌·포커스를 한꺼번에 멈춘다 | 즉시 |
| `move` | 팬 속도 −63..63 (+ = 오른쪽) | 틸트 속도 −63..63 (+ = 위) | 팬/틸트 이동. 범위 밖은 잘린다. `0 0`은 `stop` | **연속** |
| `zoom` | +1 망원 / −1 광각 / 0 정지 | 속도 0..7 (IP 경로만) | 줌. p2가 0..7 밖이면(예: `-1`) 카메라 기본 속도 | **연속** |
| `focus` | +1 원거리 / −1 근거리 / 0 정지 | — | 수동 포커스 | **연속** |
| `iris` | >0 열기 / ≤0 닫기 | — | 조리개 한 칸. AE가 MANUAL일 때만 효과 | 한 칸 |
| `preset_goto` | 1..255 | — | 프리셋 호출 | 즉시 |
| `preset_set` | 1..255 | — | 현재 위치를 프리셋으로 저장 | 즉시 |
| `preset_clear` | 1..255 | — | 프리셋 삭제 | 즉시 |
| `power` | ≠0 ON / 0 STANDBY | — | 전원 | 즉시 |
| `ae` | ≠0 **MANUAL** / 0 AUTO | — | 노출 모드 (컨트롤러 표기 IRIS AUTO/MANUAL) | 즉시 |
| `focusmode` | ≠0 **MANUAL** / 0 AUTO | — | 포커스 모드 | 즉시 |
| `awb` | ≠0 **MANUAL** / 0 AUTO | — | 화이트밸런스 모드 | 즉시 |
| `backlight` | ≠0 ON / 0 OFF | — | 역광 보정 | 즉시 |
| `onepush` | — | — | One Push AF. IP 경로는 2초 뒤 스스로 MANUAL로 되돌린다 | 즉시 |
| `rgain` | >0 올림 / ≤0 내림 | — | R 게인 한 칸 | 한 칸 |
| `bgain` | >0 올림 / ≤0 내림 | — | B 게인 한 칸 | 한 칸 |
| `shutter` | >0 올림 / ≤0 내림 | — | 셔터 한 칸 | 한 칸 |
| `bright` | >0 올림 / ≤0 내림 | — | 노출 보정(ExpComp) 한 칸 | 한 칸 |

> **`1`의 뜻이 항목마다 다르다.** `power`·`backlight`는 1이 ON이지만 `ae`·`focusmode`·`awb`는
> 1이 **MANUAL**이다. 물리 컨트롤러의 키 배치를 옮긴 결과라 바꿀 수 없다. 호출하는 쪽
> 코드에서는 숫자 대신 이름 있는 상수로 감싸 두기를 권한다.

**`zoom`의 p2를 빠뜨리면 가장 느린 가변 속도(0)가 된다.** p2가 생략되면 0으로 읽히기
때문이다. 카메라 기본 속도를 원하면 `p2=-1`을 명시한다. RS485 경로는 p2를 무시한다 - Pelco-D
줌에는 속도 필드가 없다.

**팬/틸트 속도의 하한.** 1..63은 IP 경로에서 VISCA 속도 1..24(팬)/1..20(틸트)로 비례 환산되고,
작은 값은 모두 VISCA 최저 속도 1이 된다. 그보다 느리게는 못 움직인다
([pelcoD_command.md](pelcoD_command.md) 12절).

### 3.1 연속 명령과 deadman — 반드시 지킬 것

`move`·`zoom`·`focus`는 **누르고 있는 동안 움직이는** 명령이다. 게이트웨이는 연속 명령을
받으면 **700ms**(`WEB_HOLD_TIMEOUT_MS`) 동안만 유효한 유지를 걸고, 그 안에 같은 카메라로
다음 명령이 안 오면 **스스로 `stop`을 보낸다**. 호출하는 프로그램이 죽거나 네트워크가
끊겨도 카메라가 계속 돌지 않게 하는 안전장치다.

호출자의 계약:

1. 움직이는 동안 **300ms 간격으로 같은 명령을 다시 보낸다** (제어 패널과 같은 주기).
2. 멈출 때는 **`stop`을 명시적으로 보낸다.** 안 보내도 멈추지만 최대 0.7초 더 간다.
3. 프로그램이 종료·예외로 빠질 때도 `stop`을 보내도록 `finally`에 둔다.
4. 한 번만 보내면 "0.7초짜리 움직임"이 된다. **정해진 각도로 가려면 이동이 아니라 프리셋을
   쓴다** - 절대 위치 명령은 없다.

### 3.2 보내는 속도

- **RS485 카메라에는 초당 10회 안팎이 실용 상한이다.** 게이트웨이는 물리 컨트롤러(ZU-EPC7000)와
  같은 선을 쓰므로 버스가 30ms 조용해질 때만 끼어들고, 큐는 8칸이다. 몰아서 보내면 뒤쪽이
  버려지고 물리 컨트롤러 조작까지 씹힌다.
- **여러 카메라에 같은 명령**(예: 전체 프리셋 1)은 카메라마다 따로, 100ms 남짓 간격을 두고
  보낸다. 게이트웨이에 브로드캐스트 명령은 없다.
- IP 카메라에는 이 제약이 없다.
- 게이트웨이의 HTTP 서버는 동기식이라 요청을 하나씩 처리한다. 동시 연결을 여러 개 열어도
  빨라지지 않는다. 요청 하나를 보내고 응답을 받은 뒤 다음을 보낸다.

### 3.3 물리 컨트롤러와의 관계

ZU-EPC7000은 같은 버스에 그대로 물려 있고, 게이트웨이가 그 송신을 막을 수단은 없다. 둘이
같은 카메라를 동시에 조작하면 **마지막 명령이 이긴다.** 자동화 프로그램은
`/api/state`의 `busActive`가 `true`인 동안(최근 3초 안에 컨트롤러가 버스를 씀) 조작을
미루는 식으로 양보하게 짜는 것을 권한다.

외부 명령도 "버스 활동"으로 계산된다. 그래서 외부 프로그램으로만 조작하는 동안에도 Auto
Power Control이 카메라를 Standby로 내리지 않는다.

---

## 4. 상태 — `GET /api/state`

```sh
curl -s http://192.168.41.10/api/state
```

AP로 접속해도 조회는 된다(`control`이 `false`로 온다). 응답 크기는 1.5KB 안팎이다.

### 4.1 예시

```json
{
  "control": true,
  "staUp": true,
  "sta": "192.168.41.10",
  "ap": "192.168.4.1",
  "apSsid": "RS485Gateway-72E8",
  "proto": "PEL-D",
  "baud": 9600,
  "busActive": false,
  "txQueued": 0,
  "txSent": 12,
  "txDropped": 0,
  "cams": [
    {"n":1,"path":"rs485","ip":"-","power":"ON","iris":"AUTO","awb":"AUTO","focus":"MANUAL","blc":"OFF","age":3},
    {"n":4,"path":"rs485","ip":"-","power":"STBY","iris":"-","awb":"-","focus":"-","blc":"-","age":120},
    {"n":6,"path":"ip","ip":"192.168.41.61","power":"ON","iris":"AUTO","awb":"AUTO","focus":"AUTO","blc":"OFF","age":1},
    {"n":7,"path":"rs485","ip":"-","power":"-","iris":"-","awb":"-","focus":"-","blc":"-","age":-1}
  ]
}
```

(`cams`는 실제로는 항상 1..7 일곱 개가 순서대로 온다. 위는 줄인 것이다.)

### 4.2 필드

| 필드 | 형 | 뜻 |
| --- | --- | --- |
| `control` | bool | 이 연결로 `/api/cmd`를 쓸 수 있는가 (STA 접속이면 `true`) |
| `staUp` | bool | 게이트웨이가 평소 Wi-Fi에 붙어 있는가 |
| `sta` | string | 게이트웨이 STA 주소. 끊겨 있으면 `"-"` |
| `ap` / `apSsid` | string | 설정용 AP 주소와 SSID |
| `proto` | string | RS485 입력 프로토콜: `PEL-D`, `PEL-P`, `PEL-A`(자동 판별), `VISCA` |
| `baud` | number | RS485 보드레이트 |
| `busActive` | bool | 물리 컨트롤러가 최근 3초 안에 버스를 썼는가 (3.3절) |
| `txQueued` | number | RS485 송신 큐에 대기 중인 프레임 수 |
| `txSent` | number | 부팅 후 외부/웹 명령으로 RS485에 내보낸 프레임 누계 |
| `txDropped` | number | 버스 틈을 못 잡아 버린 프레임 누계 (2.3절) |
| `cams[].n` | number | 카메라 번호 1..7 |
| `cams[].path` | string | `ip`(VISCA over IP) 또는 `rs485`(버스, 터널 슬롯 포함) |
| `cams[].ip` | string | IP 경로일 때 카메라 주소, 아니면 `"-"` |
| `cams[].power` | string | `ON` / `STBY` / `-` |
| `cams[].iris` | string | `AUTO` / `MANUAL` / `-` (AE 모드) |
| `cams[].awb` | string | `AUTO` / `MANUAL` / `-` |
| `cams[].focus` | string | `AUTO` / `MANUAL` / `-` |
| `cams[].blc` | string | `ON` / `OFF` / `-` |
| `cams[].age` | number | 위 값들을 마지막으로 관측한 뒤 지난 초. `-1` = 한 번도 관측 못 함 |

`txSent`/`txDropped`는 부팅할 때 0으로 돌아간다. 차이를 보려면 호출 전후 값을 비교한다.

### 4.3 상태 값을 믿을 수 있는 정도

- **`"-"`는 "모른다"이다.** 그럴듯한 기본값을 지어내지 않는다.
- **IP 카메라**는 게이트웨이가 1초에 한 항목씩 직접 물어 채운다. 바뀐 값은 수 초 안에
  반영된다.
- **RS485 카메라**(터널 슬롯 포함)는 게이트웨이가 직접 묻지 않는다. 물리 컨트롤러가 그
  카메라를 폴링할 때 오가는 질문과 답을 엿들어 채운다. 컨트롤러는 **선택된 카메라만**
  폴링하므로, 선택되지 않은 카메라의 값은 오래될 수 있고(`age`가 커진다), 컨트롤러가 꺼져
  있으면 영영 `-`일 수 있다. 외부 명령을 보낸 직후의 값은 실제 확인이 아니라 "보낸 값"으로
  미리 채운 것이다.
- 그래서 **RS485 카메라의 상태를 보고 분기하는 로직은 신뢰할 수 없다.** 필요하면 `age`를
  같이 보고, 오래된 값은 모르는 것으로 취급한다.

### 4.4 조회 주기

제어 패널은 1초에 한 번 부른다. 외부 프로그램도 **1초 이상** 간격을 권한다. 응답마다
JSON을 새로 만들고, HTTP 서버가 그동안 다른 요청(명령)을 처리하지 못한다.

---

## 5. 예제

### 5.1 셸

```sh
GW=192.168.41.10

# 6번 프리셋 3
curl -s -X POST http://$GW/api/cmd -d cam=6 -d action=preset_goto -d p1=3

# 1~3번 전원 ON (카메라마다 따로, 간격을 두고)
for c in 1 2 3; do
  curl -s -X POST http://$GW/api/cmd -d cam=$c -d action=power -d p1=1; sleep 0.15
done

# 상태
curl -s http://$GW/api/state
```

### 5.2 Python (표준 라이브러리만)

```python
import json, time, urllib.parse, urllib.request

GW = "http://192.168.41.10"

def cmd(cam, action, p1=0, p2=0):
    body = urllib.parse.urlencode({"cam": cam, "action": action, "p1": p1, "p2": p2}).encode()
    with urllib.request.urlopen(GW + "/api/cmd", data=body, timeout=2) as r:
        res = json.load(r)
    if not res.get("ok"):
        raise RuntimeError(res.get("err", "rejected"))

def state():
    with urllib.request.urlopen(GW + "/api/state", timeout=2) as r:
        return json.load(r)

def move_for(cam, pan, tilt, seconds):
    """deadman 계약(3.1절)을 지키는 연속 이동."""
    end = time.monotonic() + seconds
    try:
        while time.monotonic() < end:
            cmd(cam, "move", pan, tilt)
            time.sleep(0.3)
    finally:
        cmd(cam, "stop")

cmd(6, "preset_goto", 3)
move_for(6, 20, 0, 1.5)          # 1.5초 동안 천천히 오른쪽
cmd(6, "zoom", 1, -1)            # 기본 속도로 망원 (p2=-1 명시, 3절)
time.sleep(0.3); cmd(6, "stop")
print(state()["cams"][5])
```

`urllib.request.urlopen`은 HTTP 403을 예외(`HTTPError`)로 던진다. AP로 접속한 경우다.

### 5.3 JavaScript (브라우저 / Node 18+)

```js
const GW = "http://192.168.41.10";

async function cmd(cam, action, p1 = 0, p2 = 0) {
  const r = await fetch(`${GW}/api/cmd`, {
    method: "POST",
    body: new URLSearchParams({ cam, action, p1, p2 }),
  });
  const res = await r.json();
  if (!res.ok) throw new Error(res.err);
}

const state = async () => (await fetch(`${GW}/api/state`)).json();
```

게이트웨이는 CORS 헤더를 보내지 않는다. 다른 출처의 웹 페이지에서 부르면 브라우저가 응답을
막는다(`application/x-www-form-urlencoded` 단순 요청이라 명령 자체는 도착하지만 결과를 못
읽는다). 웹 페이지에서 결과까지 써야 하면 같은 LAN의 서버를 거쳐 부른다.

---

## 6. 설정 자동화 (선택)

아래는 사람용 설정 화면이 쓰는 경로다. **응답이 JSON이 아니라 HTML 페이지**이므로 결과를
기계적으로 해석하기는 어렵다. 여러 대를 같은 설정으로 맞추거나 백업을 모아 두는 용도로만
쓴다. 이 경로들은 AP/STA 구분 없이 열려 있다.

| 메서드 / 경로 | 하는 일 | 주의 |
| --- | --- | --- |
| `GET /backup/download` | 설정 전체를 `key=value` 텍스트로 받는다 (`rs485gw-XXXX-config.txt`) | **Wi-Fi 비밀번호가 평문으로 들어 있다** |
| `POST /backup` | 설정 파일 복원. `multipart/form-data`, 파일 필드 이름 **`config`**, 최대 8192바이트 | 파일에 없는 키는 현재 값을 유지한다. 적용 후 **1.5초 뒤 재부팅** |
| `POST /update` | 펌웨어 업데이트(OTA). `multipart/form-data`, 파일 필드 이름 **`firmware`** (`.bin`) | 다른 칩용 이미지는 거절된다. 끝나면 **재부팅**. 업로드 중에는 카메라 제어가 멈춘다 |
| `POST /factory-reset` | 공장 초기화 후 재부팅 | **되돌릴 수 없다.** Wi-Fi 설정까지 지워져 STA로 다시 접속할 수 없게 된다 |

설정 파일 형식(키 이름, 카메라 슬롯은 `cam1.ip`, `cam1.protocol`… 식)은
[readme.md](readme.md) 13.4.2절에 있다.

```sh
curl -s -o gw-backup.txt http://$GW/backup/download
curl -s -F config=@gw-backup.txt http://$GW/backup > /dev/null   # 복원 후 재부팅
curl -s -F firmware=@firmware.bin http://$GW/update > /dev/null  # OTA 후 재부팅
```

---

## 7. 이 인터페이스로 안 되는 것

| | |
| --- | --- |
| 절대 위치(각도) 지정 | 없다. 프리셋으로 대신한다 |
| OSD 메뉴 조작 | 컨트롤러 MENU 키의 프레임을 아직 실측하지 못했다 |
| Pelco-P 버스에서 RS485 카메라 제어 | 마스터 프레임을 Pelco-D로만 만든다 (2.2절 `err`) |
| 명령 실행 결과의 확정 | 2.3절, 4.3절 |
| 상태 변경 알림(push, WebSocket) | 없다. `/api/state`를 주기적으로 읽는다 |
| 인증 / 사용자 구분 | 없다. LAN에 붙은 것이 곧 권한이다 |
| 원시 프레임(VISCA, Pelco-D) 직접 수신 | 받지 않는다. 이유는 [cli_interface.md](cli_interface.md) 5·7절 |
| 카메라 쪽 게이트웨이(터널의 B)로 제어 | 쓰지 않는다. B에도 `/api/cmd`가 열려 있고 `ok:true`가 오지만, 카메라 쪽 역할은 RS485 송신 큐를 처리하지 않아 명령이 나가지 않는다. 터널 카메라(4번)도 **컨트롤러 쪽 게이트웨이의 `cam=4`** 로 제어한다 |

새 기능은 `webExecuteCommand()`에 `action`을 추가하는 것으로 들어온다. 그러면 제어 패널,
`ptz.py`, 외부 프로그램이 동시에 그 기능을 얻는다.
