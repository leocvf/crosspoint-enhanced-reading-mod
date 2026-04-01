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
    const bool restarted = NimBLEDevice::startAdvertising();
    manager.advertisingActive = restarted;
    if (restarted) {
      LOG_INF("BLE", "Restarted advertising after disconnect");
    } else {
      manager.lastError = "Failed to restart advertising";
      LOG_ERR("BLE", "%s", manager.lastError.c_str());
    }
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

bool BluetoothManager::start(const std::string& deviceName, PayloadCallback callback) {
  onPayload = callback;
  lastError.clear();
  if (started) {
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
  // Remote TTS transport is write-only to a known GATT characteristic.
  // Keep auth disabled so Android apps can connect and write without an explicit pair/bond step.
  NimBLEDevice::setSecurityAuth(false, false, false);
  LOG_INF("BLE", "Security auth disabled for easier app connectivity (no pairing required)");

  server = NimBLEDevice::createServer();
  if (!server) {
    lastError = "createServer failed";
    LOG_ERR("BLE", "%s", lastError.c_str());
    return false;
  }
  server->setCallbacks(new ServerCallbacks(*this));

  service = server->createService(X4_TTS_SERVICE_UUID);
  if (!service) {
    lastError = "createService failed";
    LOG_ERR("BLE", "%s", lastError.c_str());
    return false;
  }
  commandCharacteristic = service->createCharacteristic(
      X4_TTS_COMMAND_CHARACTERISTIC_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  if (!commandCharacteristic) {
    lastError = "createCharacteristic failed";
    LOG_ERR("BLE", "%s", lastError.c_str());
    return false;
  }
  commandCharacteristic->setCallbacks(new CommandCallbacks(*this));

  service->start();
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  if (!advertising) {
    lastError = "getAdvertising failed";
    LOG_ERR("BLE", "%s", lastError.c_str());
    return false;
  }
  advertising->addServiceUUID(X4_TTS_SERVICE_UUID);
  advertising->setScanResponse(true);
  advertising->setName(deviceName);
  advertisingActive = advertising->start();
  if (!advertisingActive) {
    lastError = "advertising->start failed";
    LOG_ERR("BLE", "%s", lastError.c_str());
    return false;
  }

  nextAdvertisingRetryAtMs = 0;
  started = true;
  LOG_INF("BLE", "BluetoothManager started, advertising service %s name=%s", X4_TTS_SERVICE_UUID, deviceName.c_str());
  return true;
}

void BluetoothManager::stop() {
  if (!started) {
    return;
  }

  LOG_INF("BLE", "Stopping BluetoothManager");
  if (nimbleInitialized) {
    NimBLEDevice::stopAdvertising();
    NimBLEDevice::deinit(false);
    nimbleInitialized = false;
  }

  pendingPayloads.clear();
  connected = false;
  advertisingActive = false;
  started = false;
  nextAdvertisingRetryAtMs = 0;
  server = nullptr;
  service = nullptr;
  commandCharacteristic = nullptr;
}

void BluetoothManager::poll() {
  if (started && !connected && !advertisingActive) {
    const unsigned long now = millis();
    if (nextAdvertisingRetryAtMs == 0 || now >= nextAdvertisingRetryAtMs) {
      const bool restarted = NimBLEDevice::startAdvertising();
      advertisingActive = restarted;
      if (restarted) {
        lastError.clear();
        nextAdvertisingRetryAtMs = 0;
        LOG_INF("BLE", "Recovered advertising from poll()");
      } else {
        lastError = "Advertising stopped; retry pending";
        nextAdvertisingRetryAtMs = now + 2000;
      }
    }
  }

  if (!onPayload || pendingPayloads.empty()) {
    return;
  }

  auto payloads = std::move(pendingPayloads);
  pendingPayloads.clear();

  for (const auto& payload : payloads) {
    onPayload(payload);
  }
}

void BluetoothManager::onCharacteristicWrite(const std::string& value) {
  if (pendingPayloads.size() >= MAX_PENDING_PAYLOADS) {
    LOG_ERR("BLE", "Dropping BLE payload due to full queue (%u)", static_cast<unsigned int>(MAX_PENDING_PAYLOADS));
    return;
  }
  pendingPayloads.push_back(value);
  LOG_DBG("BLE", "Received BLE payload (%u bytes)", static_cast<unsigned int>(value.size()));
}

void BluetoothManager::onConnect() {
  connected = true;
  advertisingActive = false;
  LOG_INF("BLE", "BLE client connected");
}

void BluetoothManager::onDisconnect() {
  connected = false;
  nextAdvertisingRetryAtMs = 0;
  LOG_INF("BLE", "BLE client disconnected");
}
