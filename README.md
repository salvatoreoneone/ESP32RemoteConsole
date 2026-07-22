# ESP32RemoteConsole

`ESP32RemoteConsole` mirrors an Arduino ESP32 USB serial console to one raw
TCP client and accepts the same line-based commands from USB or the network.
It also provides optional WiFiManager provisioning and ArduinoOTA lifecycle
management.

The library targets the Arduino ESP32 3.x core. It is useful when an ESP32 is
powered in place and connecting a USB cable for every diagnostic session is
inconvenient.

## Features

- `Print`-compatible output mirrored to USB and raw TCP.
- Commands accepted from either USB or the TCP client.
- Non-blocking 8 KiB network output queue.
- One active client, CR/LF handling, backspace, and disconnect events.
- Saved-network connection followed by a short serial opt-in prompt for a
  WiFiManager captive portal.
- ArduinoOTA with project-defined safety callbacks.
- Separate console and OTA servicing for timing-critical applications.
- Optional ESP32-C3 portal radio reset and TX-power override.

## Security

The raw TCP console is intentionally unauthenticated and unencrypted. OTA and
the configuration access point may also be passwordless when their password
settings are empty. Use these defaults only on a trusted private LAN; do not
forward their ports or expose the device directly to the internet.

## Installation

Download this repository as a ZIP and choose **Sketch > Include Library > Add
.ZIP Library** in Arduino IDE, or install it with Arduino CLI:

```text
arduino-cli lib install --git-url https://github.com/salvatoreoneone/ESP32RemoteConsole.git
```

WiFiManager 2.0.17 is the sole external dependency.

## Quick start

```cpp
#include <ESP32RemoteConsole.h>

using namespace esp32_remote_console;

ConsoleConfig consoleConfig;
NetworkConfig networkConfig;
RemoteConsoleKit Remote(Serial, consoleConfig, networkConfig);

void setup() {
  Serial.begin(115200);
  networkConfig.hostname = "workbench-node";
  Remote.begin();
}

void loop() {
  Remote.handle();

  String command;
  if (Remote.console().readLine(command)) {
    Remote.console().println(command);
  }
}
```

Connect PuTTY using **Raw** mode and the IP address printed at boot, on TCP
port 23 by default.

## Startup provisioning

The facade first tries saved Wi-Fi credentials for five seconds. If it cannot
connect, USB serial displays a four-second countdown. Press any key during the
countdown to start the captive portal; otherwise the application continues
offline. That choice is not saved and is offered again after the next reboot.

Call `Remote.network().startPortal()` to launch the portal later from an
application button or command.

## Timing-critical code

`Remote.handle()` services both ArduinoOTA and the TCP console. Code that must
not admit OTA while an operation is active can call only:

```cpp
Remote.console().handle();
```

The application remains responsible for deciding what a received key or a
network disconnect means.
