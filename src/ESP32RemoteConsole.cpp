#include "ESP32RemoteConsole.h"

#include <errno.h>
#include <lwip/sockets.h>

namespace esp32_remote_console {

RemoteConsole::RemoteConsole(Stream &usb, const ConsoleConfig &config)
    : usb_(usb), config_(config), server_(config.port) {}

void RemoteConsole::beginNetwork() {
  if (network_started_ || WiFi.status() != WL_CONNECTED) return;
  server_.begin(config_.port);
  server_.setNoDelay(true);
  network_started_ = true;
}

void RemoteConsole::endNetwork() {
  if (!network_started_) return;
  disconnectNetworkClientInternal(true);
  server_.end();
  network_started_ = false;
}

void RemoteConsole::handle() {
  if (!network_started_) return;

  if (network_client_active_ && !network_client_.connected()) {
    disconnectNetworkClientInternal(true);
  }

  WiFiClient incoming = server_.accept();
  if (incoming) {
    incoming.setNoDelay(true);
    if (network_client_active_) {
      incoming.print("\r\nConsole already has an active client.\r\n");
      incoming.stop();
    } else {
      network_client_ = incoming;
      network_client_active_ = true;
      network_command_length_ = 0;
      network_ignore_next_lf_ = false;
      network_ignore_lf_since_ = 0;
      clearNetworkTx();
      queueNetworkText("\r\n*** ");
      queueNetworkText(config_.banner ? config_.banner : "ESP32 remote console");
      queueNetworkText(" ***\r\nRaw TCP console; commands are project-defined.\r\n");
      if (config_.show_trusted_lan_warning) {
        queueNetworkText("WARNING: unauthenticated trusted-LAN connection.\r\n");
      }
      queueNetworkText("\r\n");
    }
  }

  flushNetworkTx();
  reportNetworkOverflow();
}

bool RemoteConsole::readLine(String &line) {
  if (readLineFrom(usb_, usb_command_, usb_command_length_,
                   usb_ignore_next_lf_, usb_ignore_lf_since_, line)) {
    return true;
  }

  return network_client_active_ &&
         readLineFrom(network_client_, network_command_,
                      network_command_length_, network_ignore_next_lf_,
                      network_ignore_lf_since_, line);
}

bool RemoteConsole::discardInput() {
  bool had_input = usb_command_length_ != 0 || network_command_length_ != 0;
  usb_command_length_ = 0;
  network_command_length_ = 0;

  had_input |= discardFrom(usb_, usb_ignore_next_lf_, usb_ignore_lf_since_);
  if (network_client_active_) {
    had_input |= discardFrom(network_client_, network_ignore_next_lf_,
                             network_ignore_lf_since_);
  }
  return had_input;
}

bool RemoteConsole::takeDisconnectEvent() {
  const bool value = disconnect_event_;
  disconnect_event_ = false;
  return value;
}

bool RemoteConsole::networkStarted() const { return network_started_; }

bool RemoteConsole::networkClientConnected() {
  return network_client_active_ && network_client_.connected();
}

void RemoteConsole::disconnectNetworkClient() {
  disconnectNetworkClientInternal(true);
}

uint16_t RemoteConsole::port() const { return config_.port; }

void RemoteConsole::flush() {
  usb_.flush();
  flushNetworkTx();
}

size_t RemoteConsole::write(uint8_t value) { return write(&value, 1); }

size_t RemoteConsole::write(const uint8_t *buffer, size_t size) {
  usb_.write(buffer, size);
  if (network_client_active_) {
    for (size_t i = 0; i < size; ++i) queueNetworkByte(buffer[i]);
  }
  return size;
}

template <typename InputType>
bool RemoteConsole::readLineFrom(InputType &input, char *buffer,
                                 size_t &length, bool &ignore_next_lf,
                                 uint32_t &ignore_lf_since, String &line) {
  while (input.available()) {
    const int raw = input.read();
    if (raw < 0) break;
    const char value = static_cast<char>(raw);

    if (ignore_next_lf) {
      const bool is_crlf_tail =
          value == '\n' &&
          static_cast<uint32_t>(millis() - ignore_lf_since) <= kCrLfGraceMs;
      ignore_next_lf = false;
      if (is_crlf_tail) continue;
    }

    if (value == '\r' || value == '\n') {
      if (value == '\r') {
        if (input.available() && input.peek() == '\n') {
          input.read();
        } else {
          ignore_next_lf = true;
          ignore_lf_since = millis();
        }
      }

      if (length == 0) continue;
      buffer[length] = '\0';
      line = buffer;
      length = 0;
      return true;
    }

    if (value == '\b' || value == 127) {
      if (length > 0) --length;
      continue;
    }

    if (value >= 32 && value <= 126 && length < kCommandCapacity - 1) {
      buffer[length++] = value;
    }
  }
  return false;
}

template <typename InputType>
bool RemoteConsole::discardFrom(InputType &input, bool &ignore_next_lf,
                                uint32_t &ignore_lf_since) {
  bool had_input = false;
  while (input.available()) {
    const int raw = input.read();
    if (raw < 0) break;
    const bool is_crlf_tail =
        ignore_next_lf && raw == '\n' &&
        static_cast<uint32_t>(millis() - ignore_lf_since) <= kCrLfGraceMs;
    if (is_crlf_tail) {
      ignore_next_lf = false;
      continue;
    }
    ignore_next_lf = false;
    had_input = true;
  }
  return had_input;
}

void RemoteConsole::disconnectNetworkClientInternal(bool signal_disconnect) {
  if (network_client_active_) {
    network_client_.stop();
    if (signal_disconnect) disconnect_event_ = true;
  }
  network_client_active_ = false;
  network_command_length_ = 0;
  network_ignore_next_lf_ = false;
  network_ignore_lf_since_ = 0;
  clearNetworkTx();
}

void RemoteConsole::clearNetworkTx() {
  network_tx_head_ = 0;
  network_tx_tail_ = 0;
  network_tx_count_ = 0;
  network_tx_overflowed_ = false;
}

void RemoteConsole::queueNetworkByte(uint8_t value) {
  if (network_tx_count_ == kNetworkTxCapacity) {
    network_tx_overflowed_ = true;
    return;
  }
  network_tx_[network_tx_head_] = value;
  network_tx_head_ = (network_tx_head_ + 1) % kNetworkTxCapacity;
  ++network_tx_count_;
}

void RemoteConsole::queueNetworkText(const char *value) {
  if (!value) return;
  while (*value) queueNetworkByte(static_cast<uint8_t>(*value++));
}

void RemoteConsole::reportNetworkOverflow() {
  static const char marker[] = "\r\n[Wi-Fi console output truncated]\r\n";
  if (!network_tx_overflowed_) return;
  if (kNetworkTxCapacity - network_tx_count_ < sizeof(marker) - 1) return;
  network_tx_overflowed_ = false;
  queueNetworkText(marker);
}

void RemoteConsole::flushNetworkTx() {
  if (!network_client_active_ || network_tx_count_ == 0) return;

  size_t contiguous = kNetworkTxCapacity - network_tx_tail_;
  size_t amount = network_tx_count_ < contiguous ? network_tx_count_ : contiguous;
  if (amount > kNetworkTxChunk) amount = kNetworkTxChunk;

  const int socket_fd = network_client_.fd();
  if (socket_fd < 0) {
    disconnectNetworkClientInternal(true);
    return;
  }

  const int written = ::send(
      socket_fd, reinterpret_cast<const char *>(&network_tx_[network_tx_tail_]),
      amount, MSG_DONTWAIT);

  if (written < 0) {
    if (errno != EAGAIN && errno != EWOULDBLOCK && errno != ENOMEM) {
      disconnectNetworkClientInternal(true);
    }
    return;
  }
  if (written == 0) return;

  network_tx_tail_ =
      (network_tx_tail_ + static_cast<size_t>(written)) % kNetworkTxCapacity;
  network_tx_count_ -= static_cast<size_t>(written);
}

WiFiOtaManager::WiFiOtaManager(Stream &usb, RemoteConsole &console,
                               const NetworkConfig &config)
    : usb_(usb), console_(console), config_(config) {}

NetworkState WiFiOtaManager::begin(const OtaCallbacks &callbacks) {
  callbacks_ = callbacks;
  chooseHostname();
  had_saved_credentials_ = wifi_manager_.getWiFiIsSaved();

  if (connectSavedNetwork()) {
    startNetworkServices();
    return state_;
  }

  if (promptForPortal()) return startPortal();

  console_.println(F("Continuing offline. Call startPortal() to configure later."));
  state_ = NetworkState::Offline;
  last_reconnect_attempt_ms_ = millis();
  return state_;
}

void WiFiOtaManager::handle() {
  const bool now_connected = WiFi.status() == WL_CONNECTED;
  if (now_connected) {
    if (!ota_started_) startNetworkServices();
    ArduinoOTA.handle();
    console_.handle();
    return;
  }

  if (ota_started_ || console_.networkStarted()) stopNetworkServices();
  tryBackgroundReconnect();
}

NetworkState WiFiOtaManager::startPortal() {
  stopNetworkServices();
  console_.print(F("Starting setup network: "));
  console_.println(portal_ssid_);
  console_.println(F("Setup page: http://192.168.4.1"));

  const char *password =
      config_.portal_password && config_.portal_password[0]
          ? config_.portal_password
          : nullptr;
  const bool connected = wifi_manager_.startConfiguredPortal(
      config_, portal_ssid_.c_str(), password);

  if (connected && WiFi.status() == WL_CONNECTED) {
    had_saved_credentials_ = true;
    startNetworkServices();
    return state_;
  }

  console_.println(F("Wi-Fi setup portal timed out; continuing offline."));
  state_ = NetworkState::PortalTimedOut;
  last_reconnect_attempt_ms_ = millis();
  return state_;
}

NetworkState WiFiOtaManager::state() const { return state_; }

bool WiFiOtaManager::connected() const {
  return WiFi.status() == WL_CONNECTED;
}

bool WiFiOtaManager::otaStarted() const { return ota_started_; }

const String &WiFiOtaManager::hostname() const { return hostname_; }

bool WiFiOtaManager::StableWiFiManager::startConfiguredPortal(
    const NetworkConfig &config, const char *ap_name,
    const char *ap_password) {
  setHostname(config.hostname);
  setConnectTimeout(config.connect_timeout_ms / 1000);
  setConfigPortalTimeout(config.portal_timeout_seconds);
  if (config.portal_channel > 0) setWiFiAPChannel(config.portal_channel);

  if (config.stable_portal_radio_reset) {
    _disableSTA = false;
    _disableSTAConn = false;
    WiFi.mode(WIFI_OFF);
    delay(500);
    WiFi.mode(WIFI_AP);
    delay(500);
    if (config.override_tx_power) WiFi.setTxPower(config.tx_power);
    delay(100);
  }

  return startConfigPortal(ap_name, ap_password);
}

void WiFiOtaManager::chooseHostname() {
  if (config_.hostname && config_.hostname[0]) {
    hostname_ = config_.hostname;
  } else {
    const uint32_t suffix = static_cast<uint32_t>(ESP.getEfuseMac());
    char generated[32];
    snprintf(generated, sizeof(generated), "esp32-console-%06lX",
             static_cast<unsigned long>(suffix & 0xFFFFFFUL));
    hostname_ = generated;
  }

  if (config_.portal_ssid && config_.portal_ssid[0]) {
    portal_ssid_ = config_.portal_ssid;
  } else {
    portal_ssid_ = hostname_ + "-Setup";
  }

  config_.hostname = hostname_.c_str();
  config_.portal_ssid = portal_ssid_.c_str();
}

void WiFiOtaManager::applyTxPower() {
  if (config_.override_tx_power) WiFi.setTxPower(config_.tx_power);
}

bool WiFiOtaManager::connectSavedNetwork() {
  WiFi.setHostname(hostname_.c_str());
  WiFi.mode(WIFI_STA);
  delay(100);
  applyTxPower();
  WiFi.setHostname(hostname_.c_str());
  WiFi.setAutoReconnect(true);

  if (!had_saved_credentials_) {
    console_.println(F("No saved Wi-Fi credentials."));
    return false;
  }

  console_.println(F("Connecting to saved Wi-Fi..."));
  WiFi.begin();
  const uint32_t started = millis();
  while (WiFi.status() != WL_CONNECTED &&
         static_cast<uint32_t>(millis() - started) <
             config_.connect_timeout_ms) {
    delay(50);
  }
  return WiFi.status() == WL_CONNECTED;
}

bool WiFiOtaManager::promptForPortal() {
  while (usb_.available()) usb_.read();
  console_.print(F("Wi-Fi unavailable. Press any key for setup portal; "));
  console_.println(F("otherwise startup continues offline."));

  for (int remaining = config_.portal_prompt_seconds; remaining > 0;
       --remaining) {
    console_.print(F("  "));
    console_.print(remaining);
    console_.println(F("..."));
    const uint32_t second_started = millis();
    while (static_cast<uint32_t>(millis() - second_started) < 1000) {
      if (usb_.available()) {
        while (usb_.available()) usb_.read();
        console_.println(F("Setup portal requested."));
        return true;
      }
      delay(20);
    }
  }
  return false;
}

void WiFiOtaManager::startNetworkServices() {
  if (WiFi.status() != WL_CONNECTED) return;
  if (ota_started_) return;

  applyTxPower();
  configureOtaCallbacks();
  ArduinoOTA.setHostname(hostname_.c_str());
  if (config_.ota_password && config_.ota_password[0]) {
    ArduinoOTA.setPassword(config_.ota_password);
  }
  ArduinoOTA.begin();
  ota_started_ = true;
  console_.beginNetwork();
  state_ = NetworkState::Connected;
  reportConnected();
}

void WiFiOtaManager::stopNetworkServices() {
  if (ota_started_) ArduinoOTA.end();
  ota_started_ = false;
  console_.endNetwork();
  state_ = NetworkState::Offline;
}

void WiFiOtaManager::configureOtaCallbacks() {
  ArduinoOTA.onStart([this]() {
    if (callbacks_.on_start) callbacks_.on_start();
    console_.println(F("OTA update starting."));
    console_.handle();
    console_.disconnectNetworkClient();
  });
  ArduinoOTA.onEnd([this]() {
    console_.println(F("\nOTA complete; restarting."));
    if (callbacks_.on_end) callbacks_.on_end();
  });
  ArduinoOTA.onProgress([this](unsigned int progress, unsigned int total) {
    if (callbacks_.on_progress) callbacks_.on_progress(progress, total);
  });
  ArduinoOTA.onError([this](ota_error_t error) {
    console_.print(F("\nOTA error: "));
    console_.println(static_cast<unsigned int>(error));
    if (callbacks_.on_error) callbacks_.on_error(error);
  });
}

void WiFiOtaManager::reportConnected() {
  console_.print(F("Wi-Fi connected: "));
  console_.println(WiFi.localIP());
  console_.print(F("OTA ready as "));
  console_.println(hostname_);
  console_.print(F("Wi-Fi console: "));
  console_.print(WiFi.localIP());
  console_.print(':');
  console_.println(console_.port());
  if (config_.show_trusted_lan_warning &&
      (!config_.ota_password || !config_.ota_password[0])) {
    console_.println(F("WARNING: OTA is passwordless; trusted LAN only."));
  }
}

void WiFiOtaManager::tryBackgroundReconnect() {
  if (!had_saved_credentials_ || config_.reconnect_interval_ms == 0) return;
  const uint32_t now = millis();
  if (static_cast<uint32_t>(now - last_reconnect_attempt_ms_) <
      config_.reconnect_interval_ms) {
    return;
  }
  last_reconnect_attempt_ms_ = now;
  WiFi.mode(WIFI_STA);
  applyTxPower();
  WiFi.reconnect();
}

RemoteConsoleKit::RemoteConsoleKit(Stream &usb,
                                   const ConsoleConfig &console_config,
                                   const NetworkConfig &network_config)
    : console_(usb, console_config), network_(usb, console_, network_config) {}

NetworkState RemoteConsoleKit::begin(const OtaCallbacks &callbacks) {
  return network_.begin(callbacks);
}

void RemoteConsoleKit::handle() { network_.handle(); }

RemoteConsole &RemoteConsoleKit::console() { return console_; }

WiFiOtaManager &RemoteConsoleKit::network() { return network_; }

}  // namespace esp32_remote_console
