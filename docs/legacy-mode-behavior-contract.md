# Legacy Mode Behavior Contract

## Accepted packets

- `{"type":"clear"}`
  - Atomically resets legacy/session state to `IDLE`.
- `{"type":"load_text","docId":"...","text":"..."}`
  - Supports chunked sends for same `docId`.
  - Chunks append until `MAX_DOC_BYTES` (16 KiB).
  - New `docId` finalizes old pending doc and starts a new buffer.
  - Finalization also occurs after 500 ms inactivity.
- `{"type":"position","docId":"...","start":N,"end":M}`
  - Ignored unless active doc is ready.
  - `docId` must equal active doc id.
  - Clamps to `[0, docLen]`, swaps if `start>end`, expands collapsed range by 1 char when possible.

## State machine

- `IDLE`
- `DOC_LOADING`
- `DOC_READY`
- `POSITIONING`
- `ERROR_RECOVERABLE`

`clear` always returns to `IDLE` and clears doc/highlight buffers.

## Framing and parser behavior

- BLE writes are treated as arbitrary fragments.
- JSON object framing is brace-balanced with string/escape awareness.
- Multiple frames per BLE write are supported.
- Parser errors drop only the bad frame; session/doc state remains active.
- Oversize frame buffer / payload increments watchdog counters and remains responsive.

## Failure behavior

- Invalid UTF-8 bytes are dropped while preserving valid prefix/suffix bytes.
- Over-limit document bytes are truncated safely.
- Position before ready or wrong `docId` is ignored.
- Repeated identical position ranges are debounced.
- Legacy highlight apply is rate-limited to 33 ms minimum interval (~30 Hz).

## Telemetry

At most once per second:
- `activeDocId`, `docLen`, `readyState`, `lastPosition`, `parseErrCount`, `dropCount`.

## Test matrix

| Case | Coverage |
|---|---|
| Chunked `load_text` assembly | `RemoteTTSLegacyModeTest::testChunkedAssemblyFinalize` |
| Doc switch mid-flight | `RemoteTTSLegacyModeTest::testDocSwitchMidFlightFinalizesOld` |
| Position before doc ready | `RemoteTTSLegacyModeTest::testPositionBeforeReadyIgnored` |
| Invalid JSON recovery | `RemoteTTSFrameParserTest` (bad frame between valid frames) |
| UTF-8 edge cases | `RemoteTTSLegacyModeTest::testUtf8AndClampBehavior` |
| Bounds clamp behavior | `RemoteTTSLegacyModeTest::testUtf8AndClampBehavior` |
