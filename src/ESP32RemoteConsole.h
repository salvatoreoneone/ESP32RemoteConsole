#pragma once

#if !defined(ARDUINO_ARCH_ESP32)
#error "ESP32RemoteConsole supports the Arduino ESP32 core only."
#endif

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <functional>

namespace esp32_remote_console {

struct ConsoleConfig {
  uint16_t port = 23;
  const char *banner = "ESP32 remote console";
  bool show_trusted_lan_warning = true;
};

struct NetworkConfig {
  const char *hostname = nullptr;
  const char *portal_ssid = nullptr;
  const char *portal_password = nullptr;
  const char *ota_password = nullptr;

  uint32_t connect_timeout_ms = 5000;
  uint8_t portal_prompt_seconds = 4;
  uint16_t portal_timeout_seconds = 180;
  uint32_t reconnect_interval_ms = 30000;

  int32_t portal_channel = 0;
  bool stable_portal_radio_reset = false;
  bool override_tx_power = false;
  wifi_power_t tx_power = WIFI_POWER_19_5dBm;
  bool show_trusted_lan_warning = true;
};

struct OtaCallbacks {
  std::function<void()> on_start;
  std::function<void()> on_end;
  std::function<void(unsigned int, unsigned int)> on_progress;
  std::function<void(ota_error_t)> on_error;
};

enum class NetworkState : uint8_t {
  Offline,
  Connected,
  PortalTimedOut,
};

class RemoteConsole : public Print {
 public:
  explicit RemoteConsole(Stream &usb, const ConsoleConfig &config = ConsoleConfig());

  void beginNetwork();
  void endNetwork();
  void handle();

  bool readLine(String &line);
  bool discardInput();
  bool takeDisconnectEvent();

  bool networkStarted() const;
  bool networkClientConnected();
  void disconnectNetworkClient();
  uint16_t port() const;

  void flush() override;
  size_t write(uint8_t value) override;
  size_t write(const uint8_t *buffer, size_t size) override;
  using Print::write;

 private:
  static constexpr size_t kNetworkTxCapacity = 8192;
  static constexpr size_t kNetworkTxChunk = 256;
  static constexpr size_t kCommandCapacity = 64;
  static constexpr uint32_t kCrLfGraceMs = 100;

  template <typename InputType>
  bool readLineFrom(InputType &input, char *buffer, size_t &length,
                    bool &ignore_next_lf, uint32_t &ignore_lf_since,
                    String &line);

  template <typename InputType>
  bool discardFrom(InputType &input, bool &ignore_next_lf,
                   uint32_t &ignore_lf_since);

  void disconnectNetworkClientInternal(bool signal_disconnect);
  void clearNetworkTx();
  void queueNetworkByte(uint8_t value);
  void queueNetworkText(const char *value);
  void reportNetworkOverflow();
  void flushNetworkTx();

  Stream &usb_;
  ConsoleConfig config_;
  WiFiServer server_;
  WiFiClient network_client_;
  bool network_started_ = false;
  bool network_client_active_ = false;
  bool disconnect_event_ = false;

  uint8_t network_tx_[kNetworkTxCapacity];
  size_t network_tx_head_ = 0;
  size_t network_tx_tail_ = 0;
  size_t network_tx_count_ = 0;
  bool network_tx_overflowed_ = false;

  char usb_command_[kCommandCapacity];
  char network_command_[kCommandCapacity];
  size_t usb_command_length_ = 0;
  size_t network_command_length_ = 0;
  bool usb_ignore_next_lf_ = false;
  bool network_ignore_next_lf_ = false;
  uint32_t usb_ignore_lf_since_ = 0;
  uint32_t network_ignore_lf_since_ = 0;
};

class WiFiOtaManager {
 public:
  WiFiOtaManager(Stream &usb, RemoteConsole &console,
                 const NetworkConfig &config = NetworkConfig());

  NetworkState begin(const OtaCallbacks &callbacks = OtaCallbacks());
  void handle();
  NetworkState startPortal();

  NetworkState state() const;
  bool connected() const;
  bool otaStarted() const;
  const String &hostname() const;

 private:
  class StableWiFiManager : public WiFiManager {
   public:
    bool startConfiguredPortal(const NetworkConfig &config,
                               const char *ap_name,
                               const char *ap_password);
  };

  void chooseHostname();
  void applyTxPower();
  bool connectSavedNetwork();
  bool promptForPortal();
  void startNetworkServices();
  void stopNetworkServices();
  void configureOtaCallbacks();
  void reportConnected();
  void tryBackgroundReconnect();

  Stream &usb_;
  RemoteConsole &console_;
  NetworkConfig config_;
  OtaCallbacks callbacks_;
  StableWiFiManager wifi_manager_;
  String hostname_;
  String portal_ssid_;
  NetworkState state_ = NetworkState::Offline;
  bool ota_started_ = false;
  bool had_saved_credentials_ = false;
  uint32_t last_reconnect_attempt_ms_ = 0;
};

class RemoteConsoleKit {
 public:
  RemoteConsoleKit(Stream &usb,
                   const ConsoleConfig &console_config = ConsoleConfig(),
                   const NetworkConfig &network_config = NetworkConfig());

  NetworkState begin(const OtaCallbacks &callbacks = OtaCallbacks());
  void handle();

  RemoteConsole &console();
  WiFiOtaManager &network();

 private:
  RemoteConsole console_;
  WiFiOtaManager network_;
};

}  // namespace esp32_remote_console
