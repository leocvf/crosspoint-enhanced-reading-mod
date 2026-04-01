#include "BluetoothManager.h"

#include <Arduino.h>
#include <Logging.h>
#include <WiFi.h>

#include "RemoteTTSConstants.h"

class BluetoothManager::ServerCallbacks : public NimBLEServerCallbacks {
 public:
  explicit ServerCallbacks(BluetoothManager& manager) : manager(manager) {}

  void onConnect(NimBLEServer*) override { manager.onConnect(); }

  void onDisconnect(NimBLEServer*) override {
    manager.onDisconnect();
    manager.advertisingRestartRequested = true;
  }

 private:
  BluetoothManager& manager;
};

class BluetoothManager::CommandCallbacks : public NimBLECharacteristicCallbacks {
 public:
  explicit CommandCallbacks(BluetoothManager& manager) : manager(manager) {}

  void onWrite(NimBLECharacteristic* characteristic) override {
    const auto value = characteristic->getValue();
    manager.onCharacteristicWrite(value);
  }

 private:
  BluetoothManager& manager;
};

BluetoothManager& BluetoothManager::instance() {
  static BluetoothManager manager;
  return manager;
}

bool BluetoothManager::isStarted() const {
  return started.load();
}

bool BluetoothManager::isConnected() const {
  return connected.load();
}

bool BluetoothManager::hasNimbleInit() const {
  return nimbleInitialized.load();
}

bool BluetoothManager::isAdvertising() const {
  return advertisingActive.load();
}

std::string BluetoothManager::getLastError() const {
  std::lock_guard<std::mutex> lock(stateMutex);
  return lastError;
}

bool BluetoothManager::start(const std::string& deviceName, PayloadCallback callback) {
  {
    std::lock_guard<std::mutex> lock(stateMutex);
    onPayload = callback;
    lastError.clear();
  }
  if (started.load()) {
    LOG_INF("BLE", "BluetoothManager already started");
    return true;
  }

  LOG_INF("BLE", "Initializing NimBLE for Remote TTS transport");
  if (WiFi.getMode() != WIFI_OFF) {
    LOG_INF("BLE", "Disabling WiFi before BLE init (ESP32-C3 coexistence)");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    delay(100);
  }

  NimBLEDevice::init(deviceName);
  nimbleInitialized = true;
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  // Require bonding + MITM protection so Android must pair before writing TTS payloads.
  NimBLEDevice::setSecurityAuth(true, false, true);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
  NimBLEDevice::setSecurityPasskey(X4_TTS_PAIRING_PASSKEY);
  LOG_INF("BLE", "Security enabled: pairing required (passkey=%06lu)",
          static_cast<unsigned long>(X4_TTS_PAIRING_PASSKEY));

  server = NimBLEDevice::createServer();
  if (!server) {
    std::lock_guard<std::mutex> lock(stateMutex);
    lastError = "createServer failed";
    LOG_ERR("BLE", "%s", lastError.c_str());
    return false;
  }
  server->setCallbacks(new ServerCallbacks(*this));

  service = server->createService(X4_TTS_SERVICE_UUID);
  if (!service) {
    std::lock_guard<std::mutex> lock(stateMutex);
    lastError = "createService failed";
    LOG_ERR("BLE", "%s", lastError.c_str());
    return false;
  }
  commandCharacteristic = service->createCharacteristic(
      X4_TTS_COMMAND_CHARACTERISTIC_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::WRITE_ENC);
  if (!commandCharacteristic) {
    std::lock_guard<std::mutex> lock(stateMutex);
    lastError = "createCharacteristic failed";
    LOG_ERR("BLE", "%s", lastError.c_str());
    return false;
  }
  commandCharacteristic->setCallbacks(new CommandCallbacks(*this));

  service->start();
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  if (!advertising) {
    std::lock_guard<std::mutex> lock(stateMutex);
    lastError = "getAdvertising failed";
    LOG_ERR("BLE", "%s", lastError.c_str());
    return false;
  }
  advertising->addServiceUUID(X4_TTS_SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->setName(deviceName);
  advertisingActive = advertising->start();
  if (!advertisingActive.load()) {
    std::lock_guard<std::mutex> lock(stateMutex);
    lastError = "advertising->start failed";
    LOG_ERR("BLE", "%s", lastError.c_str());
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(stateMutex);
    nextAdvertisingRetryAtMs = 0;
  }
  advertisingRestartRequested = false;
  started = true;
  LOG_INF("BLE", "BluetoothManager started, advertising service %s name=%s", X4_TTS_SERVICE_UUID, deviceName.c_str());
  return true;
}

void BluetoothManager::stop() {
  if (!started.load()) {
    return;
  }

  LOG_INF("BLE", "Stopping BluetoothManager");
  if (nimbleInitialized.load()) {
    NimBLEDevice::stopAdvertising();
    NimBLEDevice::deinit(false);
    nimbleInitialized = false;
  }

  {
    std::lock_guard<std::mutex> lock(stateMutex);
    pendingPayloads.clear();
    onPayload = nullptr;
    nextAdvertisingRetryAtMs = 0;
    lastError.clear();
  }
  connected = false;
  advertisingActive = false;
  started = false;
  advertisingRestartRequested = false;
  server = nullptr;
  service = nullptr;
  commandCharacteristic = nullptr;
}

void BluetoothManager::poll() {
  if (started.load() && advertisingRestartRequested.load() && !connected.load()) {
    advertisingRestartRequested = false;
    const bool restarted = NimBLEDevice::startAdvertising();
    advertisingActive = restarted;
    if (restarted) {
      std::lock_guard<std::mutex> lock(stateMutex);
      lastError.clear();
      LOG_INF("BLE", "Restarted advertising after disconnect");
    } else {
      std::lock_guard<std::mutex> lock(stateMutex);
      lastError = "Failed to restart advertising";
      nextAdvertisingRetryAtMs = millis() + 2000;
      LOG_ERR("BLE", "%s", lastError.c_str());
    }
  }

  if (started.load() && !connected.load() && !advertisingActive.load()) {
    const unsigned long now = millis();
    unsigned long nextRetryAt = 0;
    {
      std::lock_guard<std::mutex> lock(stateMutex);
      nextRetryAt = nextAdvertisingRetryAtMs;
    }
    if (nextRetryAt == 0 || now >= nextRetryAt) {
      const bool restarted = NimBLEDevice::startAdvertising();
      advertisingActive = restarted;
      if (restarted) {
        std::lock_guard<std::mutex> lock(stateMutex);
        lastError.clear();
        nextAdvertisingRetryAtMs = 0;
        LOG_INF("BLE", "Recovered advertising from poll()");
      } else {
        std::lock_guard<std::mutex> lock(stateMutex);
        lastError = "Advertising stopped; retry pending";
        nextAdvertisingRetryAtMs = now + 2000;
      }
    }
  }

  std::vector<std::string> payloads;
  PayloadCallback payloadHandler;
  {
    std::lock_guard<std::mutex> lock(stateMutex);
    if (!onPayload || pendingPayloads.empty()) {
      return;
    }
    payloads = std::move(pendingPayloads);
    pendingPayloads.clear();
    payloadHandler = onPayload;
  }

  for (const auto& payload : payloads) {
    payloadHandler(payload);
  }
}

void BluetoothManager::onCharacteristicWrite(const std::string& value) {
  std::lock_guard<std::mutex> lock(stateMutex);
  if (pendingPayloads.size() >= MAX_PENDING_PAYLOADS) {
    LOG_ERR("BLE", "Dropping BLE payload due to full queue (%u)", static_cast<unsigned int>(MAX_PENDING_PAYLOADS));
    return;
  }
  pendingPayloads.push_back(value);
  LOG_DBG("BLE", "Received BLE payload (%u bytes)", static_cast<unsigned int>(value.size()));
}

void BluetoothManager::onConnect() {
  std::lock_guard<std::mutex> lock(stateMutex);
  connected = true;
  advertisingActive = false;
  LOG_INF("BLE", "BLE client connected");
}

void BluetoothManager::onDisconnect() {
  std::lock_guard<std::mutex> lock(stateMutex);
  connected = false;
  nextAdvertisingRetryAtMs = 0;
  LOG_INF("BLE", "BLE client disconnected");
}
