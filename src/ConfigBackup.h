#pragma once

#include <Arduino.h>
#include "RoutingTable.h"

// SystemConfig 전체를 텍스트 파일 한 덩어리로 내보내고 다시 읽어들인다 (readme 13.4.2절).
// 웹 설정 화면의 Backup 페이지가 쓴다 - 설정을 한 대에서 받아 여러 대에 그대로 올리거나,
// 손볼 일이 있을 때 지금 상태를 먼저 받아두는 용도다.
//
// **JSON이 아니라 key=value를 한 줄씩 쓰는 이유.** 이 프로젝트에는 JSON 라이브러리가
// 없고(WebServer도 arduino-esp32 내장만 쓴다), 중첩 객체와 배열까지 다루는 파서를 직접
// 넣어봐야 얻는 게 없다. key=value는 파서가 짧게 끝나고, 무엇보다 현장에서 메모장으로
// 열어 고칠 수 있다 - 이 파일의 주 용도가 "여러 대를 같은 값으로 맞추기"라 그게 형식을
// 고른 기준이다.
//
// **파일에 없는 키는(값이 빈 키도) 지금 값을 그대로 둔다.** 그래서 필요 없는 줄을 지우면
// 그 항목만 빼고 복원된다 - AP 이름이나 고정 IP처럼 기기마다 달라야 하는 줄을 지우고
// 나머지만 여러 대에 뿌릴 수 있다. 설정 항목이 늘어난 펌웨어가 옛 파일을 읽는 경우도 같은
// 규칙으로 자연히 처리된다(Storage::load()가 짧은 블롭을 받아주는 것과 같은 생각이다 -
// 10.0.1절).

// 파일 첫 줄에 들어가는 형식 표시. 업로드된 것이 정말 이 게이트웨이의 설정 파일인지
// 확인하는 데 쓴다 - 이게 없으면 엉뚱한 파일을 올렸을 때 "0개 적용됨"이라는 알 수 없는
// 결과만 나오고, 왜 아무 일도 안 일어났는지 화면이 설명할 수 없다.
extern const char kConfigBackupFormatLine[];

// cfg를 설정 파일 텍스트로 만들어 out에 이어붙인다.
void serializeConfig(const SystemConfig& cfg, String& out);

struct ConfigRestoreResult {
  // false면 cfg는 **전혀 건드려지지 않았다** - 형식 표시가 없는 파일을 통째로 거부한
  // 경우다. 이때만 error가 채워진다.
  bool ok = false;
  String error;

  uint16_t applied = 0;
  uint16_t skipped = 0;
  // 건너뛴 줄의 사유. 한 줄에 하나씩, '\n'으로 구분한다. 화면에 그대로 내보낸다.
  String notes;
};

// text를 파싱해 cfg에 덮어쓴다.
//
// 해석되는 항목만 반영하고 나머지는 건너뛴다 - 값 하나가 잘못됐다고 파일 전체를 버리면
// 손으로 고친 파일에서 오타 하나 때문에 나머지 서른 줄을 다시 넣어야 한다. 대신 건너뛴
// 줄은 빠짐없이 notes에 남겨 화면에 보여준다.
//
// **cfg를 직접 고치지 않고 사본에 채운 뒤 마지막에 한 번 옮긴다.** 중간에 거부 판정이
// 나도 반쯤 적용된 설정이 남지 않게 하기 위함이다.
ConfigRestoreResult restoreConfig(const String& text, SystemConfig& cfg);
