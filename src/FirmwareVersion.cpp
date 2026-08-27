#include "FirmwareVersion.h"

#include "generated/FirmwareInfo.h"

// 빌드마다 내용이 바뀌는 생성 헤더를 포함하는 유일한 파일이다. 그래서 매 빌드에서
// 다시 컴파일되는 것도 이 파일 하나뿐이다 (FirmwareVersion.h 주석 참고).

const char* firmwareVersion() {
#if FIRMWARE_GIT_DIRTY
  // 커밋되지 않은 변경이 섞인 빌드라는 뜻이다 - 리비전만으로는 소스를 재현할 수 없다.
  return FIRMWARE_GIT_REV "+dirty (" FIRMWARE_GIT_BRANCH ")";
#else
  return FIRMWARE_GIT_REV " (" FIRMWARE_GIT_BRANCH ")";
#endif
}

const char* firmwareBuildTime() {
  return FIRMWARE_BUILD_TIME;
}
