# Implementation Notes — X4 Remote TTS MVP

## Architecture findings
- App boot and activity routing are in `src/main.cpp`.
- Home menu entry points are in `src/activities/home/HomeActivity.*`.
- Reader activities are isolated under `src/activities/reader/`.
- Rendering is activity-local and triggered via `requestUpdate()`/render task in `src/activities/Activity.*`.
- E-ink refresh mode is controlled by `renderer.displayBuffer(...)`.
- NimBLE and ArduinoJson dependencies are already present in `platformio.ini`.

## BLE reference findings applied
After inspecting `thedrunkpenguin/crosspoint-reader-ble`:
- `BluetoothHIDManager` is the core BLE lifecycle owner in that fork.
- Its enable flow disables WiFi before BLE init (important on ESP32-C3).
- It configures BLE runtime defaults (power/PHY/security) immediately after init.
- BLE events are callback-driven and manager-scoped, with UI/settings delegating lifecycle to manager.

## Safest integration path
1. Keep a dedicated activity for Remote TTS mode (no EPUB reader rewrite).
2. Keep BLE in a dedicated generalized manager.
3. Initialize BLE only when entering Remote TTS mode.
4. Parse and apply minimal JSON commands to simple state.
5. Render wrapped plain text and highlight with conservative refresh.

## Generalized Bluetooth manager integration plan
- Keep `BluetoothManager` in `lib/hal` as transport-focused manager.
- Preserve lifecycle/logging style from the reference manager pattern.
- Reuse safe coexistence init behavior (disable WiFi before BLE init).
- Keep HID/page-turn concepts out of this class.
- Use one writable characteristic for JSON command payloads.

## File-by-file implementation plan
- `lib/hal/RemoteTTSConstants.h`: placeholder UUID constants.
- `lib/hal/BluetoothManager.h/.cpp`: generalized BLE transport manager.
- `src/activities/reader/RemoteTTSReaderActivity.h/.cpp`: isolated mode UI, parser, state, renderer.
- `src/activities/home/HomeActivity.h/.cpp`: add entry in home menu.
- `src/main.cpp`: wire new navigation callback.
- `README_REMOTE_TTS_READER.md`: behavior/protocol/testing doc.
