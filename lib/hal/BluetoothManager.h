#pragma once

#include <NimBLEDevice.h>

#include <functional>
#include <string>
#include <vector>

class BluetoothManager {
 public:
  using PayloadCallback = std::function<void(const std::string& payload)>;

  static BluetoothManager& instance();

  bool start(const std::string& deviceName, PayloadCallback callback);
  void stop();
  void poll();

  bool isStarted() const { return started; }
  bool isConnected() const { return connected; }
  bool hasNimbleInit() const { return nimbleInitialized; }
  bool isAdvertising() const { return advertisingActive; }
  const std::string& getLastError() const { return lastError; }

 private:
  class ServerCallbacks;
  class CommandCallbacks;

  BluetoothManager() = default;
  BluetoothManager(const BluetoothManager&) = delete;
  BluetoothManager& operator=(const BluetoothManager&) = delete;

  bool started = false;
  bool connected = false;
  bool nimbleInitialized = false;
  bool advertisingActive = false;
  std::string lastError;
  PayloadCallback onPayload;
  NimBLEServer* server = nullptr;
  NimBLEService* service = nullptr;
  NimBLECharacteristic* commandCharacteristic = nullptr;
  std::vector<std::string> pendingPayloads;
  static constexpr size_t MAX_PENDING_PAYLOADS = 16;
  unsigned long nextAdvertisingRetryAtMs = 0;

  void onCharacteristicWrite(const std::string& value);
  void onConnect();
  void onDisconnect();
};
