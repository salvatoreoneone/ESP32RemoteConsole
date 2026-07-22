#include <ESP32RemoteConsole.h>

using namespace esp32_remote_console;

ConsoleConfig consoleConfig;

NetworkConfig makeNetworkConfig() {
  NetworkConfig config;
  config.hostname = "esp32-console-example";
  config.portal_ssid = "ESP32-Console-Setup";
  return config;
}

RemoteConsoleKit Remote(Serial, consoleConfig, makeNetworkConfig());

void setup() {
  Serial.begin(115200);
  delay(300);

  OtaCallbacks ota;
  ota.on_start = []() {
    // Put actuators into a safe state here.
  };

  Remote.begin(ota);
  Remote.console().println("Type 'status' or 'portal'.");
}

void loop() {
  Remote.handle();

  String command;
  if (!Remote.console().readLine(command)) return;

  if (command == "status") {
    Remote.console().print("Wi-Fi: ");
    Remote.console().println(Remote.network().connected() ? "connected" : "offline");
  } else if (command == "portal") {
    Remote.network().startPortal();
  } else {
    Remote.console().print("Unknown command: ");
    Remote.console().println(command);
  }
}
