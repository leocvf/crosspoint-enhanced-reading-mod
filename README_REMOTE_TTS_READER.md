# Remote TTS Reader (MVP)

## What was added
- A new standalone mode/activity: **Remote TTS Reader**.
- BLE transport manager for receiving UTF-8 JSON writes.
- JSON command handling for `ping`, `clear`, `load_text`, and `position`.
- Plain-text wrapped rendering with active-range highlighting.

## Files changed
- `lib/hal/RemoteTTSConstants.h`
- `lib/hal/BluetoothManager.h`
- `lib/hal/BluetoothManager.cpp`
- `src/activities/reader/RemoteTTSReaderActivity.h`
- `src/activities/reader/RemoteTTSReaderActivity.cpp`
- `src/activities/home/HomeActivity.h`
- `src/activities/home/HomeActivity.cpp`
- `src/main.cpp`
- `BLE_REFERENCE_NOTES.md`
- `IMPLEMENTATION_NOTES_X4_TTS.md`

## Protocol summary
Packets are UTF-8 JSON written to `X4_TTS_COMMAND_CHARACTERISTIC_UUID`.

Supported commands:
- `{"type":"ping"}`
- `{"type":"clear"}`
- `{"type":"load_text","docId":"...","text":"..."}`
- `{"type":"position","docId":"...","start":N,"end":M}`

Behavior:
- malformed JSON rejected
- missing required fields rejected
- `position` range is normalized/clamped
- `position` for mismatched `docId` is ignored

## Placeholder UUIDs
- `X4_TTS_SERVICE_UUID`
- `X4_TTS_COMMAND_CHARACTERISTIC_UUID`

Both live in `lib/hal/RemoteTTSConstants.h` and are placeholders.

## BLE adaptation notes
- The generalized transport in `lib/hal/BluetoothManager.*` was adapted from the BLE fork's `BluetoothHIDManager` lifecycle approach (centralized manager, callback-driven events, explicit start/stop behavior).
- During adaptation, WiFi/BLE coexistence handling and NimBLE init tuning (power/PHY/security) were carried over in simplified form for Remote TTS server use.

## HID-specific pieces intentionally not reused
- HID report maps
- key mapping/device-profile logic
- page-turn specific input translation

## Serial-first test flow
1. Enter `Remote TTS Reader` from Home.
2. Confirm logs for BLE init/start/advertising.
3. Send payloads and verify parse logs/state logs/render updates.

## Android packet test flow
1. Connect Android BLE client to X4 advertisement.
2. Write JSON to command characteristic.
3. Send `load_text` then `position` updates and verify highlighted range movement.
4. Send `clear` and verify screen reset state.

## Known limitations
- No response characteristic/ACK path yet.
- Single simple layout and highlight style.
- Text storage is in-memory only.
- UUIDs are placeholders.

## Suggested next step for Android AutoHighlightTTS
- Add optional ack/error notification characteristic and sequence IDs so Android can verify command application and retry failed writes deterministically.
