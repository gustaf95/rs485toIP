#!/usr/bin/env python3
"""외부 프로그램/스크립트에서 게이트웨이의 카메라를 제어하는 CLI (doc/cli_interface.md).

게이트웨이 펌웨어에는 아무것도 추가하지 않는다 - 제어 패널이 쓰는 그 HTTP API
(`POST /api/cmd`, `GET /api/state`)를 그대로 부른다. 그래서 이 파일은 PC 쪽에만 있으면
되고, 지금 현장에 올라가 있는 펌웨어에서 바로 동작한다.

    export PTZ_GW=192.168.0.50
    ptz.py 6 preset goto 3
    ptz.py 6 move 40 0 --for 2
    ptz.py 6 awb auto
    ptz.py all power on
    ptz.py state

왜 셸 래퍼가 아니라 파이썬 한 파일인가: 이 저장소에 이미 파이썬이 있고(embed_web.py),
표준 라이브러리만 쓰면 라즈베리파이든 윈도우 콘솔 PC든 복사 한 번으로 끝난다. curl이
없는 윈도우에서도 돌아야 한다.

종료 코드:
    0  성공
    1  게이트웨이가 명령을 거절함 (사유는 stderr에 문장으로)
    2  통신 실패 또는 사용법 오류
"""

import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

EXIT_OK = 0
EXIT_REJECTED = 1
EXIT_FAILED = 2

CAM_MIN = 1
CAM_MAX = 7  # config.h CAMERA_SLOT_COUNT

TIMEOUT_S = 4.0

# 연속 명령(move/zoom/focus)을 --for로 유지할 때의 재전송 간격. 게이트웨이의 deadman은
# 700ms(WEB_HOLD_TIMEOUT_MS)라 그보다 넉넉히 짧아야 한다 - 제어 패널이 쓰는 값과 같다.
HOLD_REPEAT_S = 0.3

# 계단식 명령(iris/gain/shutter/bright)을 --repeat로 여러 칸 움직일 때의 간격.
STEP_REPEAT_S = 0.2

# all로 7대를 연달아 부를 때의 간격. RS485 송신 큐는 깊이 8이고 버스가 30ms 조용해질
# 때까지 기다렸다가 끼어들므로(doc/cli_interface.md 8.3절), 한꺼번에 밀면 뒤쪽이
# 버려진다. 게이트웨이에 브로드캐스트를 넣지 않고 여기서 간격을 두는 이유다.
ALL_GAP_S = 0.12


class Usage(Exception):
    """사용법 오류. 메시지를 그대로 보여주고 2로 끝낸다."""


class Rejected(Exception):
    """게이트웨이가 거절함. err 문장을 그대로 보여주고 1로 끝낸다."""


# ---------------------------------------------------------------------------
# HTTP
# ---------------------------------------------------------------------------

def gateway_url(explicit):
    gw = explicit or os.environ.get("PTZ_GW")
    if not gw:
        raise Usage(
            "게이트웨이 주소를 모른다. PTZ_GW 환경변수를 설정하거나 --gw 를 쓴다.\n"
            "  export PTZ_GW=192.168.0.50        (Windows: set PTZ_GW=192.168.0.50)\n"
            "주소는 설정 화면 Status 또는 USB Serial 메뉴 '1. Network Settings'에 있다."
        )
    if not gw.startswith("http://") and not gw.startswith("https://"):
        gw = "http://" + gw
    return gw.rstrip("/")


def post_cmd(base, cam, action, p1=0, p2=0):
    """POST /api/cmd. 성공하면 None, 거절이면 Rejected를 던진다.

    주의: 게이트웨이는 명령 거절도 HTTP 200에 {"ok":false}로 돌려준다(제어 패널이 오류
    문장을 그대로 띄우려고 그렇게 두었다). 그래서 상태 코드가 아니라 ok 필드를 봐야
    한다 - 이 함수가 그걸 종료 코드로 바꿔주는 자리다.
    """
    body = urllib.parse.urlencode(
        {"cam": cam, "action": action, "p1": p1, "p2": p2}
    ).encode("ascii")
    req = urllib.request.Request(base + "/api/cmd", data=body, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=TIMEOUT_S) as resp:
            payload = json.loads(resp.read().decode("utf-8", "replace"))
    except urllib.error.HTTPError as e:
        # AP로 접속하면 403이 온다 - 본문에 사유가 들어 있으니 그대로 보여준다.
        try:
            payload = json.loads(e.read().decode("utf-8", "replace"))
        except Exception:
            raise Rejected("HTTP %s" % e.code)
        raise Rejected(payload.get("err", "HTTP %s" % e.code))
    except Exception as e:
        raise SystemExit2("게이트웨이에 연결하지 못했다 (%s): %s" % (base, e))

    if not payload.get("ok"):
        raise Rejected(payload.get("err", "(사유 없음)"))


def get_state(base):
    try:
        with urllib.request.urlopen(base + "/api/state", timeout=TIMEOUT_S) as resp:
            return json.loads(resp.read().decode("utf-8", "replace"))
    except Exception as e:
        raise SystemExit2("게이트웨이에 연결하지 못했다 (%s): %s" % (base, e))


class SystemExit2(Exception):
    """통신 실패. 2로 끝낸다."""


# ---------------------------------------------------------------------------
# 명령 어휘
# ---------------------------------------------------------------------------
# doc/cli_interface.md 9절의 표가 유일한 출처다. 여기서는 그 표의 숫자를 낱말로
# 감싸기만 한다 - 숫자를 그대로 노출하면 반드시 헷갈리기 때문이다: power 1은 ON인데
# ae 1은 MANUAL이다. 물리 컨트롤러의 키 배치를 옮긴 결과라 게이트웨이 쪽 숫자는
# 바꿀 수 없고, 대신 바깥으로 나가는 이름을 사람 말로 둔다.

def _word(name, value, table):
    key = value.lower()
    if key not in table:
        raise Usage("%s: %s 중 하나여야 한다 (받은 값: %s)"
                    % (name, " | ".join(sorted(table)), value))
    return table[key]


def _int(name, value, lo, hi):
    try:
        n = int(value, 10)
    except ValueError:
        raise Usage("%s: 정수여야 한다 (받은 값: %s)" % (name, value))
    if not lo <= n <= hi:
        raise Usage("%s: %d..%d 범위여야 한다 (받은 값: %d)" % (name, lo, hi, n))
    return n


def _need(args, n, usage):
    if len(args) < n:
        raise Usage("인자가 모자란다: " + usage)
    return args[:n]


# 각 함수는 (action, p1, p2, kind)를 돌려준다.
#   kind "hold" = 누르고 있는 동안 움직이는 명령 (--for 를 받는다)
#   kind "step" = 한 번에 한 칸 움직이는 명령 (--repeat 를 받는다)
#   kind "once" = 한 방으로 끝나는 명령

def c_stop(a):
    return ("stop", 0, 0, "once")


def c_move(a):
    pan, tilt = _need(a, 2, "move <pan -63..63> <tilt -63..63>")
    return ("move", _int("pan", pan, -63, 63), _int("tilt", tilt, -63, 63), "hold")


def c_zoom(a):
    d = _need(a, 1, "zoom in|out|stop [속도 0..7]")[0]
    p1 = _word("zoom", d, {"in": 1, "tele": 1, "out": -1, "wide": -1, "stop": 0})
    # 속도를 안 주면 -1 - 게이트웨이가 실측으로 확인된 고정속을 쓴다.
    # RS485 경로에는 애초에 속도 개념이 없어 무시된다.
    p2 = _int("speed", a[1], 0, 7) if len(a) > 1 else -1
    return ("zoom", p1, p2, "hold")


def c_focus(a):
    d = _need(a, 1, "focus near|far|stop")[0]
    return ("focus", _word("focus", d, {"near": -1, "far": 1, "stop": 0}), 0, "hold")


def c_iris(a):
    d = _need(a, 1, "iris open|close")[0]
    return ("iris", _word("iris", d, {"open": 1, "close": -1}), 0, "step")


def c_preset(a):
    op, n = _need(a, 2, "preset goto|set|clear <1..255>")
    action = _word("preset", op, {"goto": "preset_goto", "set": "preset_set",
                                  "clear": "preset_clear"})
    return (action, _int("preset", n, 1, 255), 0, "once")


def _onoff(action, name):
    def fn(a):
        v = _need(a, 1, "%s on|off" % name)[0]
        return (action, _word(name, v, {"on": 1, "off": 0}), 0, "once")
    return fn


def _automanual(action, name):
    def fn(a):
        v = _need(a, 1, "%s auto|manual" % name)[0]
        return (action, _word(name, v, {"auto": 0, "manual": 1}), 0, "once")
    return fn


def _updown(action, name):
    def fn(a):
        v = _need(a, 1, "%s up|down" % name)[0]
        return (action, _word(name, v, {"up": 1, "down": -1}), 0, "step")
    return fn


def c_onepush(a):
    return ("onepush", 0, 0, "once")


def c_raw(a):
    """어휘에 없는 것을 그대로 보내는 탈출구. 새 action이 생겨도 이 파일을 안 고쳐도 된다."""
    parts = _need(a, 1, "raw <action> [p1] [p2]")
    action = parts[0]
    p1 = _int("p1", a[1], -2147483648, 2147483647) if len(a) > 1 else 0
    p2 = _int("p2", a[2], -2147483648, 2147483647) if len(a) > 2 else 0
    return (action, p1, p2, "once")


COMMANDS = {
    "stop": c_stop,
    "move": c_move,
    "zoom": c_zoom,
    "focus": c_focus,
    "iris": c_iris,
    "preset": c_preset,
    "power": _onoff("power", "power"),
    "backlight": _onoff("backlight", "backlight"),
    "blc": _onoff("backlight", "blc"),
    "ae": _automanual("ae", "ae"),
    "iris-mode": _automanual("ae", "iris-mode"),
    "awb": _automanual("awb", "awb"),
    "focusmode": _automanual("focusmode", "focusmode"),
    "af": _automanual("focusmode", "af"),
    "onepush": c_onepush,
    "rgain": _updown("rgain", "rgain"),
    "bgain": _updown("bgain", "bgain"),
    "shutter": _updown("shutter", "shutter"),
    "bright": _updown("bright", "bright"),
    "raw": c_raw,
}


# ---------------------------------------------------------------------------
# 실행
# ---------------------------------------------------------------------------

def run_one(base, cam, action, p1, p2, kind, hold_s, repeat):
    """카메라 한 대에 명령을 보낸다. 거절이면 Rejected가 올라간다."""
    if kind == "hold" and hold_s is not None:
        # 게이트웨이는 명령 하나당 700ms짜리 임대만 준다(WEB_HOLD_TIMEOUT_MS). 브라우저가
        # 300ms마다 갱신하듯 여기서도 갱신해야 --for 초 동안 계속 움직인다. 이 재전송을
        # 스크립트 작성자가 직접 하게 두면 반드시 잊어버리므로 CLI가 대신 한다.
        deadline = time.monotonic() + hold_s
        try:
            while True:
                post_cmd(base, cam, action, p1, p2)
                if time.monotonic() >= deadline:
                    break
                time.sleep(HOLD_REPEAT_S)
        finally:
            # 임대 만료를 기다리지 않고 명시적으로 멈춘다 - 안 그러면 최대 700ms 더 간다.
            # finally인 이유는 Ctrl-C다: 사용자가 도는 카메라를 멈추려고 누르는 것인데
            # 그때 정지를 안 보내면 오히려 700ms를 더 돌고 멈춘다.
            try:
                post_cmd(base, cam, "stop", 0, 0)
            except (Rejected, SystemExit2):
                pass
        return

    if kind == "step" and repeat > 1:
        for i in range(repeat):
            if i:
                time.sleep(STEP_REPEAT_S)
            post_cmd(base, cam, action, p1, p2)
        return

    post_cmd(base, cam, action, p1, p2)


def print_state(state, as_json):
    if as_json:
        print(json.dumps(state, ensure_ascii=False, indent=2))
        return

    print("GW %s   %s.%s   %s%s" % (
        state.get("sta", "-"),
        state.get("proto", "-"),
        state.get("baud", "-"),
        "CTRL ACTIVE" if state.get("busActive") else "BUS IDLE",
        "" if state.get("control") else "   [AP 접속 - 제어 차단됨]",
    ))
    print("%-4s %-16s %-6s %-7s %-7s %-7s %-5s %s"
          % ("CAM", "PATH", "POWER", "IRIS", "AWB", "FOCUS", "BLC", "AGE"))
    for c in state.get("cams", []):
        age = c.get("age", -1)
        print("%-4s %-16s %-6s %-7s %-7s %-7s %-5s %s" % (
            c.get("n"),
            c.get("ip") if c.get("path") == "ip" else "RS485 bus",
            c.get("power", "-"), c.get("iris", "-"), c.get("awb", "-"),
            c.get("focus", "-"), c.get("blc", "-"),
            "-" if age < 0 else "%ds" % age,
        ))
    # txDropped가 올라간다는 것은 RS485 버스에 못 끼어들어 명령이 버려졌다는 뜻이다.
    # 스크립트를 돌린 뒤 이 줄을 확인하는 습관이 필요하다(doc/cli_interface.md 8.3절).
    print("web tx sent %s / queued %s / dropped %s"
          % (state.get("txSent"), state.get("txQueued"), state.get("txDropped")))
    if any(c.get("age", -1) < 0 for c in state.get("cams", [])):
        print("(AGE '-' = 아직 한 번도 관측되지 않음. RS485 카메라의 상태는 물리")
        print(" 컨트롤러가 폴링해 줄 때만 알 수 있어, 컨트롤러가 꺼져 있으면 계속 '-'다.)")


HELP = """\
사용법: ptz.py [--gw 주소] <카메라> <명령> [인자...]
        ptz.py [--gw 주소] state [--json]

카메라: 1..7 또는 all

이동/줌/포커스 (누르고 있는 동안 움직이는 명령. --for 초 를 주면 그동안 유지한다)
  <n> move <pan -63..63> <tilt -63..63>   pan +는 오른쪽, tilt +는 위
  <n> zoom in|out|stop [속도 0..7]
  <n> focus near|far|stop
  <n> stop

프리셋
  <n> preset goto|set|clear <1..255>

모드
  <n> power on|off
  <n> ae auto|manual          IRIS AUTO/MANUAL 키와 같다 (실제로는 AE 모드)
  <n> awb auto|manual
  <n> af auto|manual          focusmode 로도 쓸 수 있다
  <n> backlight on|off        blc 로도 쓸 수 있다
  <n> onepush                 One Push AF

한 칸씩 (--repeat N 으로 여러 칸)
  <n> iris open|close
  <n> rgain|bgain|shutter|bright up|down

기타
  <n> raw <action> [p1] [p2]  어휘에 없는 명령을 그대로 보낸다
  state [--json]

옵션
  --gw 주소     게이트웨이 주소. 없으면 환경변수 PTZ_GW 를 쓴다
  --for 초      이동/줌/포커스를 그 시간 동안 유지한다 (0.3초마다 재전송 후 정지)
  --repeat N    한 칸씩 움직이는 명령을 N번 보낸다

예
  ptz.py 6 preset goto 3
  ptz.py 6 move 40 0 --for 2
  ptz.py 6 zoom in 3 --for 1.5
  ptz.py 6 bright up --repeat 3
  ptz.py all power on
  ptz.py state

--for 를 안 주면 명령이 한 번만 나가고 카메라는 약 0.7초 뒤 스스로 멈춘다 - 게이트웨이가
브라우저나 스크립트가 죽어도 카메라가 계속 돌지 않도록 걸어두는 안전장치다.
각도를 정확히 재현하려면 이동이 아니라 프리셋을 쓴다.

종료 코드: 0 성공 / 1 게이트웨이가 거절 / 2 통신 실패 또는 사용법 오류
"""


def main(argv):
    gw = None
    hold_s = None
    repeat = 1
    as_json = False
    rest = []

    i = 0
    while i < len(argv):
        a = argv[i]
        if a in ("-h", "--help", "help"):
            print(HELP)
            return EXIT_OK
        elif a == "--gw":
            if i + 1 >= len(argv):
                raise Usage("--gw 뒤에 주소가 없다")
            gw = argv[i + 1]
            i += 2
        elif a == "--for":
            if i + 1 >= len(argv):
                raise Usage("--for 뒤에 초가 없다")
            try:
                hold_s = float(argv[i + 1])
            except ValueError:
                raise Usage("--for: 숫자여야 한다 (받은 값: %s)" % argv[i + 1])
            if hold_s <= 0:
                raise Usage("--for: 0보다 커야 한다")
            i += 2
        elif a == "--repeat":
            if i + 1 >= len(argv):
                raise Usage("--repeat 뒤에 횟수가 없다")
            repeat = _int("--repeat", argv[i + 1], 1, 100)
            i += 2
        elif a == "--json":
            as_json = True
            i += 1
        else:
            rest.append(a)
            i += 1

    if not rest:
        print(HELP)
        return EXIT_OK

    base = gateway_url(gw)

    if rest[0].lower() == "state":
        print_state(get_state(base), as_json)
        return EXIT_OK

    if len(rest) < 2:
        raise Usage("카메라와 명령이 필요하다. 'ptz.py help' 참고.")

    target = rest[0].lower()
    if target == "all":
        cams = list(range(CAM_MIN, CAM_MAX + 1))
    else:
        cams = [_int("카메라", rest[0], CAM_MIN, CAM_MAX)]

    name = rest[1].lower()
    if name not in COMMANDS:
        raise Usage("모르는 명령: %s. 'ptz.py help' 참고." % rest[1])
    action, p1, p2, kind = COMMANDS[name](rest[2:])

    if hold_s is not None and kind != "hold":
        raise Usage("--for 는 move/zoom/focus 에만 쓴다 (%s 는 한 방으로 끝나는 명령)" % name)
    if len(cams) > 1 and hold_s is not None:
        # all + --for 는 "7대가 동시에 움직인다"로 읽히지만 실제로는 순차 실행이라
        # 1번이 다 끝난 뒤 2번이 움직인다. 그 오해가 예배 중에 드러나는 것보다 여기서
        # 거절하는 편이 낫다.
        raise Usage("all 과 --for 는 같이 못 쓴다 - 순차 실행이라 동시에 움직이지 않는다.\n"
                    "카메라별로 따로 부르거나, 프리셋을 쓴다.")

    failures = []
    for idx, cam in enumerate(cams):
        if idx:
            time.sleep(ALL_GAP_S)
        try:
            run_one(base, cam, action, p1, p2, kind, hold_s, repeat)
        except Rejected as e:
            if len(cams) == 1:
                raise
            failures.append((cam, str(e)))

    if failures:
        for cam, err in failures:
            sys.stderr.write("CAM%d: %s\n" % (cam, err))
        return EXIT_REJECTED
    return EXIT_OK


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except Usage as e:
        sys.stderr.write("%s\n" % e)
        sys.exit(EXIT_FAILED)
    except Rejected as e:
        sys.stderr.write("거절됨: %s\n" % e)
        sys.exit(EXIT_REJECTED)
    except SystemExit2 as e:
        sys.stderr.write("%s\n" % e)
        sys.exit(EXIT_FAILED)
    except KeyboardInterrupt:
        sys.exit(EXIT_FAILED)
