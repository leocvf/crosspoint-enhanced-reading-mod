# BLE Reference Notes (thedrunkpenguin/crosspoint-reader-ble)

## Reference files inspected
- `lib/hal/BluetoothHIDManager.h`
- `lib/hal/BluetoothHIDManager.cpp`
- `lib/hal/DeviceProfiles.cpp`
- `src/activities/settings/BluetoothSettingsActivity.cpp`
- `platformio.ini`

## Bluetooth stack/library used
- The reference fork uses **NimBLE-Arduino** (`h2zero/NimBLE-Arduino`) and organizes BLE logic inside `BluetoothHIDManager`.
- This repo also already includes NimBLE in `platformio.ini`, so no new BLE dependency was required.

## Initialization pattern in the reference fork
From `BluetoothHIDManager.cpp` in the reference fork:
- BLE enable flow explicitly disables WiFi first on ESP32-C3 before `NimBLEDevice::init(...)`.
- NimBLE parameters are configured during init, including TX power, PHY preference, and security auth settings.
- Lifecycle is centralized in one manager (`enable()/disable()` plus scan/connect/input processing paths).

## Connection lifecycle pattern in the reference fork
From `BluetoothHIDManager.cpp`:
- Connection/disconnection are callback-driven with explicit logs.
- Manager keeps clear in-memory state for active devices and recent activity timestamps.
- Disconnect handling is explicit and designed to recover cleanly.

## Settings/activity integration pattern in the reference fork
From `BluetoothSettingsActivity.cpp`:
- Settings UI interacts with the BLE manager, not with low-level NimBLE objects directly.
- UI controls manager lifecycle and connection flow while manager owns BLE state.

## What was generalized for Remote TTS in this repo
Adapted from the reference manager approach:
- Kept a dedicated singleton BLE manager class (`BluetoothManager`) with centralized BLE lifecycle and callback wiring.
- Kept callback-based connection/write event handling and explicit logs.
- Added WiFi-off-before-BLE initialization in `BluetoothManager::start()` to mirror the ESP32-C3 coexistence safety pattern.
- Added NimBLE power/PHY/security setup in `BluetoothManager::start()` based on reference initialization behavior.
- Added secure pairing for Remote TTS using a fixed passkey and encrypted characteristic writes so mobile clients pair before sending text/position packets.

## HID-specific pieces intentionally removed or isolated
Not reused from reference `BluetoothHIDManager`:
- HID service/report-map discovery and subscriptions.
- HID report parsing and keyboard/consumer-page extraction.
- `DeviceProfiles` key mapping logic.
- Page-turn button injection and reader key event translation.
- Bond/reconnect policies specific to HID remote controls.

## Minimal refactor strategy used here
- Instead of carrying HID concepts forward, the generalized manager is transport-only:
  - start/stop BLE server lifecycle
  - advertise one Remote TTS service
  - receive writes on one command characteristic
  - queue payloads for safe processing in activity loop
- Reader mode remains isolated in `RemoteTTSReaderActivity` to avoid regressions in EPUB/TXT flow.

## Dependencies/config changes required
- No extra dependency was needed because NimBLE and ArduinoJson were already present.
- Added placeholder UUID constants in `lib/hal/RemoteTTSConstants.h`:
  - `X4_TTS_SERVICE_UUID`
  - `X4_TTS_COMMAND_CHARACTERISTIC_UUID`
