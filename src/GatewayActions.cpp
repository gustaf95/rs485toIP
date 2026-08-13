#include "GatewayActions.h"
#include <WiFi.h>
#include "config.h"

void performFactoryReset(RoutingTable& routing, Storage& storage, Rs485Port& rs485,
                          StatusLed& statusLed) {
  routing.applyDefaults();
  SystemConfig& cfg = routing.get();
  storage.save(cfg);
  rs485.begin(cfg.rs485Baudrate, cfg.rs485RxPin, cfg.rs485TxPin, cfg.rs485DeRePin,
              cfg.rs485Invert);
  statusLed.begin(cfg.statusLedPin);

  Serial.flush();
  delay(300);
  ESP.restart();
}

void buildPelcoDTestCommand(uint8_t out[7]) {
  out[0] = PELCO_D_START_BYTE;
  out[1] = 0x01;
  out[2] = 0x00;
  out[3] = 0x51;
  out[4] = 0x00;
  out[5] = 0x00;

  uint8_t sum = 0;
  for (uint8_t i = 1; i < 6; i++) sum += out[i];
  out[6] = sum;
}

void buildPelcoPTestCommand(uint8_t out[8]) {
  out[0] = PELCO_P_START_BYTE;
  out[1] = 0x01;
  out[2] = 0x00;
  out[3] = 0x51;
  out[4] = 0x00;
  out[5] = 0x00;
  out[6] = PELCO_P_ETX_BYTE;

  uint8_t x = 0;
  for (uint8_t i = 1; i < 6; i++) x ^= out[i];
  out[7] = x;
}

void applyApSettings(const SystemConfig& cfg) {
  WiFi.softAP(cfg.wifi.apSsid, cfg.wifi.apPassword);
}
