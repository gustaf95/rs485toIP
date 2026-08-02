#pragma once

#include <Arduino.h>

// 상태 LED 제어 (핀은 begin()에서 지정, RS485 Settings에서 런타임에 바꿀 수 있다).
// Wi-Fi가 연결되어 있으면 항상 켜짐, 끊겨 있으면 1초 간격으로 깜박인다. RS485로
// 완성된 VISCA 패킷을 수신하면 0.2초 간격으로 2회 깜박인 뒤 원래 상태(Wi-Fi 연결
// 여부에 따른 패턴)로 복귀한다. loop()에서 update()를 매 회전 호출해야 하며,
// delay()를 쓰지 않는 non-blocking 상태 머신이다.
class StatusLed {
 public:
  void begin(uint8_t pin);

  // 매 loop() 회전마다 호출한다. wifiConnected는 현재 Wi-Fi 연결 상태.
  void update(bool wifiConnected);

  // RS485에서 완성된 VISCA 패킷을 수신했을 때 호출한다.
  void notifyRs485Signal();

 private:
  enum class Mode { WIFI_STATUS, RS485_BLINK };

  uint8_t _pin = 2;
  Mode _mode = Mode::WIFI_STATUS;
  bool _ledOn = false;

  unsigned long _lastToggleMs = 0;
  uint8_t _rs485BlinkStep = 0;
};
