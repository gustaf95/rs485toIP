#include "GatewayActions.h"
#include <WiFi.h>
#include "config.h"

void performFactoryReset(RoutingTable& routing, Storage& storage, Rs485Port& rs485,
                          StatusLed& statusLed) {
  routing.applyDefaults();
  SystemConfig& cfg = routing.get();
  storage.save(cfg);
  rs485.begin(cfg.rs485Baudrate, cfg.rs485RxPin, cfg.rs485TxPin, cfg.rs485DeRePin,
              cfg.rs485Uart0Shared);
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

void setRs485Uart0SharedMode(RoutingTable& routing, Storage& storage, Rs485Port& rs485,
                              bool enabled) {
  SystemConfig& cfg = routing.get();
  cfg.rs485Uart0Shared = enabled;
  if (enabled) {
    cfg.rs485RxPin = RS485_UART0_SHARED_RX_PIN;
    cfg.rs485TxPin = RS485_UART0_SHARED_TX_PIN;
    cfg.rs485DeRePin = RS485_UART0_SHARED_DE_RE_PIN;
    cfg.debugMode = false;
  } else {
    cfg.rs485RxPin = RS485_RX_PIN_DEFAULT;
    cfg.rs485TxPin = RS485_TX_PIN_DEFAULT;
    cfg.rs485DeRePin = RS485_DE_RE_PIN_DEFAULT;
  }
  storage.save(cfg);

  Serial.flush();
  delay(300);
  ESP.restart();
}
