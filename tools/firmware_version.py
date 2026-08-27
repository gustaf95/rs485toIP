"""빌드 시각과 git 정보를 src/generated/FirmwareInfo.h로 굽는 PlatformIO pre-build 스크립트.

왜 이렇게 하나:

- **지금 도는 펌웨어가 어느 소스에서 나왔는지 화면에서 바로 보여야 한다.** OTA로 올린
  뒤에 "정말 새 게 올라갔나"를 확인할 방법이 이것 말고는 없다(readme 13.4.1절).
- **`-D` 빌드 플래그로 넣지 않는다.** 그러면 커밋할 때마다 플래그가 바뀌어 PlatformIO가
  프로젝트를 통째로 다시 컴파일한다. 헤더로 두면 그걸 포함한 파일만 다시 짓는데,
  이 값을 쓰는 곳을 src/FirmwareVersion.cpp 하나로 몰아뒀으므로 매 빌드 다시 짓는 건
  그 작은 파일 하나뿐이다.
- 생성물은 빌드 산출물이라 git에 넣지 않는다(.gitignore의 src/generated/).

git이 없거나 저장소가 아니어도 빌드는 계속된다 - 그때는 값이 "unknown"이 된다.
"""

import datetime
import os
import subprocess

Import("env")  # noqa: F821  (PlatformIO가 주입한다)

PROJECT_DIR = env["PROJECT_DIR"]  # noqa: F821
OUT_PATH = os.path.join(PROJECT_DIR, "src", "generated", "FirmwareInfo.h")


def git(*args):
    """git 출력을 문자열로. 실패하면 빈 문자열."""
    try:
        out = subprocess.check_output(
            ["git"] + list(args), cwd=PROJECT_DIR, stderr=subprocess.DEVNULL
        )
        return out.decode("utf-8", "replace").strip()
    except Exception:
        return ""


def is_dirty():
    """커밋되지 않은 변경이 있는지.

    `git status --porcelain`을 쓰지 않는다 - 그건 stat 캐시를 보기 때문에, 내용이
    같아도 타임스탬프만 바뀐 파일(클라우드 동기화 폴더에서 흔하다)을 수정된 것으로
    report한다. 실제로 이 저장소에서 그 오탐이 확인돼서, 내용을 비교하는
    `git diff --quiet`로 판정한다.
    """
    try:
        changed = subprocess.call(
            ["git", "diff", "--quiet", "HEAD"], cwd=PROJECT_DIR, stderr=subprocess.DEVNULL
        )
    except Exception:
        return False
    if changed != 0:
        return True
    # 추적되지 않은 새 소스 파일도 빌드에 들어가므로 dirty로 본다.
    return bool(git("ls-files", "--others", "--exclude-standard", "src"))


def main():
    rev = git("rev-parse", "--short", "HEAD") or "unknown"
    branch = git("rev-parse", "--abbrev-ref", "HEAD") or "unknown"
    dirty = is_dirty()
    build_time = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")

    header = "\n".join(
        [
            "// 자동 생성 파일 - 편집하지 말 것.",
            "// tools/firmware_version.py가 매 빌드마다 다시 만든다.",
            "#pragma once",
            "",
            '#define FIRMWARE_GIT_REV "%s"' % rev,
            '#define FIRMWARE_GIT_BRANCH "%s"' % branch,
            "#define FIRMWARE_GIT_DIRTY %d" % (1 if dirty else 0),
            '#define FIRMWARE_BUILD_TIME "%s"' % build_time,
            "",
        ]
    )

    print(
        "firmware_version: %s%s (%s) built %s"
        % (rev, "+dirty" if dirty else "", branch, build_time)
    )

    os.makedirs(os.path.dirname(OUT_PATH), exist_ok=True)
    with open(OUT_PATH, "w", encoding="utf-8") as f:
        f.write(header)


main()
