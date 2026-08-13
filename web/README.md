# web/ — 제어 패널 화면 원본

`control.html`이 `/control` 페이지의 **원본**이다. 빌드할 때
[`tools/embed_web.py`](../tools/embed_web.py)가 이 파일을 gzip으로 압축해
`src/generated/WebAssets.h`(PROGMEM 배열)로 굽고, `WebControl.cpp`가 그걸 그대로 내보낸다.

**고치는 방법**: `control.html`을 고치고 평소처럼 빌드/업로드하면 된다. 별도의 변환이나
파일시스템 업로드 단계는 없다.

```
web/control.html  →  (pre-build) tools/embed_web.py  →  src/generated/WebAssets.h  →  펌웨어
```

`src/generated/`는 빌드 산출물이라 git에 넣지 않는다.

## 왜 LittleFS를 안 쓰나

파일시스템에 올리면 UI만 따로 업데이트할 수 있다는 장점이 있지만, **펌웨어와 데이터를
따로 업로드해야 한다**(`pio run -t upload`와 `pio run -t uploadfs`). 한쪽만 올리면 화면이
조용히 낡은 채로 남고, 그 상태가 겉으로 드러나지 않는다. OTA가 없어서 어차피 USB를 꽂아야
하므로 "따로 올릴 수 있다"는 이점도 실현되지 않는다.

## 브라우저에서 미리 보기 (ESP32 없이)

이 폴더에는 `api/state` 라는 가짜 응답 파일이 같이 있다. 정적 서버를 이 폴더에 띄우면
화면이 그 값으로 채워져서, 레이아웃과 CSS 작업을 실기기 없이 할 수 있다.

```sh
cd web
python -m http.server 8000
# 브라우저에서 http://localhost:8000/control.html
```

`api/state`의 값을 바꾸면 화면이 어떻게 보이는지 바로 확인할 수 있다 — 예를 들어
`"control": false`로 두면 AP 접속 시의 잠금 화면이, `"age": -1`로 두면 아직 관측 못 한
카메라의 `-` 표시가 나온다.

버튼을 눌러도 `POST /api/cmd`는 404가 나므로 아무 일도 일어나지 않는다(화면에 오류만
잠깐 뜬다). 동작 확인은 실기기에서 해야 한다.

파일을 열 때 `file://`로 직접 열지 말 것 — `fetch('/api/state')`가 동작하지 않아 화면이
빈 채로 남는다.
