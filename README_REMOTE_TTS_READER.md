# Remote TTS Reader Protocol (Streaming Upgrade)

## Protocol schema (JSON examples)

### Legacy/MVP (still supported)
- `ping`
```json
{"type":"ping"}
```
- `clear`
```json
{"type":"clear"}
```
- `load_text`
```json
{"type":"load_text","docId":"book-42","text":"Full text payload..."}
```
- `position`
```json
{"type":"position","docId":"book-42","start":120,"end":138}
```

### Streaming v2 (new)
- `stream_start` (required before chunks)
```json
{"type":"stream_start","sessionId":"s-1001","docId":"book-42","startOffset":0}
```
Optional metadata accepted on `stream_start`: `streamVersion`, `totalChars`, `startSeq`.
- `stream_chunk` (can arrive out-of-order)
```json
{"type":"stream_chunk","sessionId":"s-1001","seq":11,"offset":2048,"text":"sentence-aligned chunk"}
```
Optional metadata accepted on `stream_chunk`: `checksum`, `chunkBytes`.
- `stream_commit` (only committed contiguous chunks become renderable)
```json
{"type":"stream_commit","sessionId":"s-1001","uptoSeq":11}
```
- `stream_seek` (move render pointer/highlight focus)
```json
{"type":"stream_seek","sessionId":"s-1001","offset":2210}
```
Optional field: `resetSeq` to force receiver resync to a new sequence baseline.
- `stream_end`
```json
{"type":"stream_end","sessionId":"s-1001","reason":"eof"}
```

## Behavior guarantees
- Strict command validation: malformed packets are rejected and logged.
- Required-field validation is strict, but unknown/extra fields are ignored for forward compatibility (for example, `reason` on `stream_chunk`).
- JSON size guard: packets larger than firmware max are rejected.
- Backward compatibility: `load_text` + `position` behavior remains available.
- Streaming mode isolates receive pointer from render pointer.
- Commit model prevents partial-word artifacts by exposing only committed contiguous ranges.
- Out-of-order chunk reception is supported through sequence-keyed pending storage.
- Duplicate chunk `seq` values are deduplicated.
- Memory budget is enforced with deterministic prefix eviction on committed text plus pending cap.
- Highlight mapping uses global offsets; when target offset is outside buffered window, highlight degrades to nearest available range and increments miss counters.
- Encrypted write path is required on command characteristic; passkey remains `123456`.

## Feedback/ACK channel
- BLE now exposes an optional feedback characteristic (`X4_TTS_FEEDBACK_CHARACTERISTIC_UUID`).
- Firmware emits ACK/NACK-equivalent telemetry through internal hook logs:
  - highest contiguous sequence
  - short missing sequence preview
  - stream buffer fill percent
  - commit latency stats
- Notification wiring exists for future Android enablement.
- Firmware now emits best-effort `{"type":"ack",...}` notifications; senders should still remain idempotent and retry-safe if notify path is unavailable.
- ACK notify payload includes `sequenceId` (alias of `highestContiguousSeq`) for sender retry loops expecting sequence-based progress.
- Android-side ACK ingestion must be explicitly wired from BLE notify callbacks into the sender bridge for true end-to-end retry control.

## Security notes
- Command writes require encrypted transport; Android should pair/bond before sending commands.
- Firmware passkey is fixed to `123456`; app-side UX may rely on platform pairing dialog unless an explicit passkey flow is implemented.

## Sender algorithm recommendation
Use **sentence-aware chunking + sliding window + paced send**:
1. Pre-index sentence boundaries.
2. Build chunks near soft byte target with sentence-first cuts.
3. Maintain moving window around current spoken offset.
4. Pace BLE writes with token bucket.
5. Commit in order on receiver.
6. Drive highlight using global offsets mapped into local buffered text.

## Migration path (MVP -> streaming)
1. Keep existing discovery/connect/pair flow unchanged.
2. Replace periodic `load_text` calls with one `stream_start` followed by `stream_chunk` + `stream_commit`.
3. Continue sending `position` if convenient; firmware maps it in both modes.
4. Add `stream_seek` on scrub/seek events.
5. Send `stream_end` on EOF/session close.
6. Keep legacy fallback for older firmware by feature-gating on sender side.
7. Treat contract as dual-mode: legacy mode (`load_text`/`position`) and streaming mode (`stream_*`) are both accepted; choose one mode per active session for predictable behavior.

## Validation plan

### Serial test scenario (firmware logs)
1. Enter `Remote TTS Reader` activity.
2. Connect BLE client and pair with passkey `123456`.
3. Send `stream_start`.
4. Send chunk seq order: `1,3,2` with valid offsets.
5. Send `stream_commit uptoSeq=3` and verify logs show gap recovery + contiguous commit.
6. Send duplicate `seq=3` and verify duplicate counter increments.
7. Send `position` outside current buffer and verify highlight miss counter increments.
8. Disconnect and verify session reset log.

### Android interop matrix
- **Normal**: in-order chunks, regular commits.
- **High-rate**: 20–40 writes/sec with token bucket pacing.
- **Packet loss simulation**: intentionally drop every Nth chunk, ensure commit waits and missing seq appears in ACK logs.
- **Reconnect mid-session**: disconnect/reconnect then restart with new `sessionId`.
- **Seek storm**: rapid `stream_seek` + `position` updates.

## Troubleshooting

| Symptom | Likely cause | What to check | Suggested fix |
|---|---|---|---|
| Chunks received but text not advancing | Missing `stream_commit` or commit gap | `ack_hook` logs for `highContig` and `missing` | Resend missing seq range, then commit again |
| Highlight jumps backward | Global offsets outside buffered window | `Highlight misses` counter | Increase sender sliding window around speech offset |
| BLE writes fail before streaming starts | Encryption not established | Pairing logs and Android bond state | Pair first with passkey `123456`, then retry writes |
| Stream restarts unexpectedly | Disconnect-triggered session reset | `Streaming session reset (disconnect)` log | Reconnect and issue fresh `stream_start` |
| Render feels too flickery | Updates too frequent | Observe fast refresh cadence | Coalesce sender commits and reduce commit frequency |
