#include "StatusLed.h"

namespace {
const unsigned long kWifiBlinkIntervalMs = 1000;
const unsigned long kRs485BlinkIntervalMs = 200;
const uint8_t kRs485BlinkSteps = 4;  // ON->OFF->ON->OFF = 2회 깜박임
}  // namespace

void StatusLed::begin(uint8_t pin) {
  _pin = pin;
  pinMode(_pin, OUTPUT);
  digitalWrite(_pin, LOW);
}

void StatusLed::notifyRs485Signal() {
  _mode = Mode::RS485_BLINK;
  _rs485BlinkStep = 0;
  _lastToggleMs = millis();
  _ledOn = true;
  digitalWrite(_pin, HIGH);
}

void StatusLed::update(bool wifiConnected) {
  unsigned long now = millis();

  if (_mode == Mode::RS485_BLINK) {
    if (now - _lastToggleMs < kRs485BlinkIntervalMs) return;

    _lastToggleMs = now;
    _rs485BlinkStep++;
    if (_rs485BlinkStep >= kRs485BlinkSteps) {
      _mode = Mode::WIFI_STATUS;
      _ledOn = wifiConnected;
      digitalWrite(_pin, _ledOn ? HIGH : LOW);
    } else {
      _ledOn = (_rs485BlinkStep % 2 == 0);  // 짝수 스텝 = ON, 홀수 스텝 = OFF
      digitalWrite(_pin, _ledOn ? HIGH : LOW);
    }
    return;
  }

  // Mode::WIFI_STATUS
  if (wifiConnected) {
    if (!_ledOn) {
      _ledOn = true;
      digitalWrite(_pin, HIGH);
    }
  } else if (now - _lastToggleMs >= kWifiBlinkIntervalMs) {
    _lastToggleMs = now;
    _ledOn = !_ledOn;
    digitalWrite(_pin, _ledOn ? HIGH : LOW);
  }
}
