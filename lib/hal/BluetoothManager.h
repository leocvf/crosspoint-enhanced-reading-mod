#pragma once

#include <NimBLEDevice.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

class BluetoothManager {
 public:
  using PayloadCallback = std::function<void(const std::string& payload)>;

  static BluetoothManager& instance();

  bool start(const std::string& deviceName, PayloadCallback callback);
  void stop();
  void poll();

  bool isStarted() const;
  bool isConnected() const;
  bool hasNimbleInit() const;
  bool isAdvertising() const;
  std::string getLastError() const;

 private:
  class ServerCallbacks;
  class CommandCallbacks;

  BluetoothManager() = default;
  BluetoothManager(const BluetoothManager&) = delete;
  BluetoothManager& operator=(const BluetoothManager&) = delete;

  std::atomic<bool> started = false;
  std::atomic<bool> connected = false;
  std::atomic<bool> nimbleInitialized = false;
  std::atomic<bool> advertisingActive = false;
  std::string lastError;
  PayloadCallback onPayload;
  NimBLEServer* server = nullptr;
  NimBLEService* service = nullptr;
  NimBLECharacteristic* commandCharacteristic = nullptr;
  std::vector<std::string> pendingPayloads;
  mutable std::mutex stateMutex;
  static constexpr size_t MAX_PENDING_PAYLOADS = 16;
  unsigned long nextAdvertisingRetryAtMs = 0;
  std::atomic<bool> advertisingRestartRequested = false;

  void onCharacteristicWrite(const std::string& value);
  void onConnect();
  void onDisconnect();
};
