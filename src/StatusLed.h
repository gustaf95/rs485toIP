#pragma once

#include <Arduino.h>

// 상태 LED 제어 (핀/극성은 begin()에서 지정, RS485 Settings에서 런타임에 바꿀 수 있다).
// Wi-Fi가 연결되어 있으면 항상 켜짐, 끊겨 있으면 1초 간격으로 깜박인다. RS485로
// 완성된 VISCA 패킷을 수신하면 0.2초 간격으로 2회 깜박인 뒤 원래 상태(Wi-Fi 연결
// 여부에 따른 패턴)로 복귀한다. loop()에서 update()를 매 회전 호출해야 하며,
// delay()를 쓰지 않는 non-blocking 상태 머신이다.
class StatusLed {
 public:
  // activeLow=true면 LOW일 때 LED가 켜진다. ESP32-C3 Super Mini의 온보드 LED(GPIO8)가
  // 3V3 -> LED -> GPIO 배선이라 이쪽이고, 보통의 외부 LED(GPIO -> LED -> GND)는 반대다.
  // 극성을 잘못 주면 "안 켜진다"가 아니라 "계속 켜져 있고 가끔 꺼진다"로 보인다.
  void begin(uint8_t pin, bool activeLow);

  // 매 loop() 회전마다 호출한다. wifiConnected는 현재 Wi-Fi 연결 상태.
  void update(bool wifiConnected);

  // RS485에서 완성된 VISCA 패킷을 수신했을 때 호출한다.
  void notifyRs485Signal();

 private:
  enum class Mode { WIFI_STATUS, RS485_BLINK };

  // 논리 상태(on/off)를 극성에 맞는 실제 레벨로 바꿔 쓴다. 호출부가 상태가 바뀐
  // 순간에만 부르므로 - update()는 loop() 매 회전마다 불리지만 대부분의 회전에서는
  // 아무것도 하지 않는다 - 여기서 다시 걸러내지 않는다.
  void write(bool on);

  uint8_t _pin = 2;
  bool _activeLow = false;
  Mode _mode = Mode::WIFI_STATUS;
  bool _ledOn = false;

  unsigned long _lastToggleMs = 0;
  uint8_t _rs485BlinkStep = 0;
};
