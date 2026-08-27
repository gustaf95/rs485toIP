#pragma once

// 지금 도는 펌웨어가 어느 소스에서 나왔는지. 설정 화면(Serial/Web)이 표시한다.
//
// 값은 tools/firmware_version.py가 빌드마다 src/generated/FirmwareInfo.h로 굽고,
// 그 헤더를 포함하는 곳은 FirmwareVersion.cpp **하나뿐**이다 - 빌드마다 바뀌는 값이라
// 여러 파일이 포함하면 그 파일들이 전부 매번 다시 컴파일된다.

// "f992a84 (RS485toIP_ESP32C3mini)" 형태. 커밋되지 않은 변경이 있으면 "+dirty"가 붙는다.
// git을 못 읽으면 "unknown"이다.
const char* firmwareVersion();

// "2026-08-27 21:45". OTA로 올린 뒤 이 값이 바뀌었는지가 새 펌웨어가 실제로 부팅했다는
// 증거다 (readme 13.4.1절).
const char* firmwareBuildTime();
