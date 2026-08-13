"""web/control.html을 gzip 압축해 PROGMEM 배열 헤더로 굽는 PlatformIO pre-build 스크립트.

왜 이렇게 하나:

- **편집은 진짜 .html 파일로** 한다. C++ 문자열 안에 있으면 문법 강조도, 포매터도,
  브라우저 미리보기도 못 쓴다 (web/README.md의 미리보기 방법 참고).
- **런타임에는 파일시스템을 쓰지 않는다.** LittleFS/SPIFFS로 두면 펌웨어와 데이터를
  따로 업로드해야 해서, 한쪽만 올리면 화면이 조용히 낡은 채로 남는다. 빌드 시 굽으면
  펌웨어 하나만 올리면 되고 둘이 어긋날 수가 없다.
- **gzip**으로 굽는다. 제어 패널은 앞으로 커질 화면이고, 브라우저는 전부
  Content-Encoding: gzip을 받는다. 보통 3~4배 줄어든다.

생성물(src/generated/WebAssets.h)은 빌드 산출물이라 git에 넣지 않는다.
"""

import gzip
import os

Import("env")  # noqa: F821  (PlatformIO가 주입한다)

PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
SOURCES = [
    # (소스 파일, 심볼 이름)
    (os.path.join(PROJECT_DIR, "web", "control.html"), "CONTROL_HTML_GZ"),
]
OUT_PATH = os.path.join(PROJECT_DIR, "src", "generated", "WebAssets.h")


def render(path, symbol):
    with open(path, "rb") as f:
        raw = f.read()
    # mtime=0으로 고정해야 내용이 같을 때 출력도 같다 - 안 그러면 빌드할 때마다 헤더가
    # 바뀌어 WebControl.cpp가 매번 다시 컴파일된다.
    packed = gzip.compress(raw, compresslevel=9, mtime=0)

    lines = [
        "// %s (%d bytes raw, %d bytes gzip)"
        % (os.path.basename(path), len(raw), len(packed)),
        "const uint8_t %s[] PROGMEM = {" % symbol,
    ]
    for i in range(0, len(packed), 16):
        chunk = packed[i : i + 16]
        lines.append("    " + "".join("0x%02X," % b for b in chunk))
    lines.append("};")
    lines.append("const size_t %s_LEN = sizeof(%s);" % (symbol, symbol))
    return "\n".join(lines), len(raw), len(packed)


def main():
    body = []
    for path, symbol in SOURCES:
        if not os.path.isfile(path):
            raise SystemExit("embed_web.py: missing source file %s" % path)
        text, raw_len, gz_len = render(path, symbol)
        body.append(text)
        print("embed_web: %s %d -> %d bytes (gzip)" % (os.path.basename(path), raw_len, gz_len))

    header = "\n\n".join(
        [
            "// 자동 생성 파일 - 편집하지 말 것.",
            "// tools/embed_web.py가 web/*.html로부터 매 빌드마다 다시 만든다.",
            "#pragma once",
            "",
            "#include <Arduino.h>",
            "\n\n".join(body),
            "",
        ]
    )

    os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
    # 내용이 같으면 다시 쓰지 않는다 - 파일 타임스탬프가 바뀌면 scons가 불필요한
    # 재컴파일을 한다.
    if os.path.isfile(OUT_PATH):
        with open(OUT_PATH, "r", encoding="utf-8") as f:
            if f.read() == header:
                return
    with open(OUT_PATH, "w", encoding="utf-8") as f:
        f.write(header)


main()
