#pragma once

#include <Arduino.h>
#include <WebServer.h>
#include "DeviceConfig.h"
#include "Diagnostics.h"
#include "config.h"

// Dashboard / Wi-Fi 설정 / Target Device 설정 / 진단 화면을 제공하는
// 동기식(WebServer) 웹 설정 UI. Wi-Fi 저장 시 재부팅하여 새 설정으로 재접속한다.
class WebConfigServer {
 public:
  WebConfigServer(DeviceConfig& config, Diagnostics& diagnostics)
      : _config(config), _diagnostics(diagnostics), _server(WEB_SERVER_PORT) {}

  void begin();
  void handleClient();

 private:
  DeviceConfig& _config;
  Diagnostics& _diagnostics;
  WebServer _server;

  void handleDashboard();
  void handleWifiGet();
  void handleWifiPost();
  void handleDevicesGet();
  void handleDeviceSavePost();
  void handleDeviceDeletePost();
  void handleDiagnostics();
  void handleNotFound();

  String pageShell(const String& title, const String& body);
  String navBar();
};
