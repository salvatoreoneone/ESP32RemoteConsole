#include <ESP32RemoteConsole.h>
#include <WiFi.h>

using namespace esp32_remote_console;

const char *WIFI_SSID = "replace-me";
const char *WIFI_PASSWORD = "replace-me";

ConsoleConfig consoleConfig;
RemoteConsole Console(Serial, consoleConfig);

void setup() {
  Serial.begin(115200);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) delay(100);

  Console.beginNetwork();
  Console.println("USB and raw TCP output are now mirrored.");
}

void loop() {
  Console.handle();

  String command;
  if (Console.readLine(command)) {
    Console.print("Received: ");
    Console.println(command);
  }
}
