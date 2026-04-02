#pragma once

#include <ArduinoJson.h>

#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "RemoteTTSLegacyMode.h"
#include "RemoteTTSFrameParser.h"

class RemoteTTSReaderActivity : public Activity {
 public:
  explicit RemoteTTSReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                   const std::function<void()>& onExitToHome)
      : Activity("RemoteTTS", renderer, mappedInput), onExitToHome(onExitToHome) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&&) override;
  bool preventAutoSleep() override { return true; }

 private:
  struct State {
    bool active = false;
    std::string currentDocId;
    std::string text;
    int highlightStart = 0;
    int highlightEnd = 0;
    bool textDirty = true;
    bool highlightDirty = true;
    int renderWindowStart = 0;
    int renderWindowEnd = 0;
  } state;

  struct WrappedLine {
    std::string text;
    int start = 0;
    int end = 0;
  };

  struct StreamChunk {
    uint32_t seq = 0;
    int offset = 0;
    std::string text;
    uint32_t receivedAtMs = 0;
  };

  struct StreamStats {
    uint32_t chunksReceived = 0;
    uint32_t duplicateChunks = 0;
    uint32_t gapEvents = 0;
    uint32_t highlightMisses = 0;
    uint32_t malformedPackets = 0;
    uint32_t commits = 0;
    uint32_t commitLatencyMs = 0;
    uint32_t maxCommitLatencyMs = 0;
    uint32_t lastBufferFillPct = 0;
    uint32_t totalCharsInWindow = 0;
  };

  std::vector<WrappedLine> wrappedLines;
  const std::function<void()> onExitToHome;
  std::string debugLine1;
  std::string debugLine2;
  std::string lastCommandSummary;
  uint32_t commandCount = 0;
  int viewportFirstLine = 0;
  bool autoFollowHighlight = true;
  bool lastConnectedState = false;
  bool lastAdvertisingState = false;
  bool lastRenderWasFullRefresh = true;
  unsigned long lastRenderMs = 0;

  bool streamMode = false;
  RemoteTTSLegacyMode legacyMode;
  std::string streamSessionId;
  std::string streamDocId;
  int64_t highestContiguousSeq = -1;
  uint32_t lastCommitSeq = 0;
  uint32_t streamStartSeq = 1;
  int committedBaseOffset = 0;
  std::string committedText;
  std::map<int, std::string> deferredCommitted;
  std::map<uint32_t, StreamChunk> pendingChunks;
  std::set<uint32_t> seenChunkSeq;
  StreamStats stats;
  uint32_t streamCommitStartedAtMs = 0;
  int renderPointerGlobal = 0;

  static constexpr size_t MAX_JSON_BYTES = 768;
  static constexpr size_t MAX_FRAME_BUFFER_BYTES = 2048;
  static constexpr unsigned long LEGACY_POSITION_RENDER_INTERVAL_MS = 33;

  RemoteTTSFrameParser frameParser{MAX_FRAME_BUFFER_BYTES};
  unsigned long lastDocMismatchLogMs = 0;
  unsigned long lastTelemetryLogMs = 0;
  unsigned long lastStateLogMs = 0;
  unsigned long lastLegacyPositionApplyMs = 0;
  bool hasPendingLegacyPosition = false;
  int pendingLegacyStart = 0;
  int pendingLegacyEnd = 0;

  static constexpr size_t MAX_PENDING_CHUNKS = 96;
  static constexpr size_t MAX_STREAM_BYTES = 64 * 1024;
  static constexpr size_t MAX_COMMITTED_BYTES = 48 * 1024;
  /** Slightly faster than full refresh cadence; still eases e-ink flash on rapid highlight updates. */
  static constexpr unsigned long RENDER_COALESCE_MS = 100;

  void handlePayload(const std::string& payload);
  void processJsonFrame(const std::string& frame);
  void applyPendingLegacyPosition(unsigned long nowMs, bool force = false);
  void logLegacyTelemetryThrottled(unsigned long nowMs);
  void handleCommand(const JsonDocument& doc);
  void wrapText(int maxWidth);
  void setDemoContent();
  void clearLoadedContent();
  void setDebugMessage(const std::string& line1, const std::string& line2 = "");

  void resetStreamingSession(const std::string& reason);
  void handleStreamStart(const JsonDocument& doc);
  void handleStreamChunk(const JsonDocument& doc);
  void handleStreamCommit(const JsonDocument& doc);
  void handleStreamSeek(const JsonDocument& doc);
  void handleStreamEnd(const JsonDocument& doc);
  void commitChunk(const StreamChunk& chunk);
  void stitchDeferredCommitted();
  void enforceStreamMemoryBudget();
  void rebuildRenderWindow();
  void mapHighlightToRenderWindow(int globalStart, int globalEnd);
  void emitAckLogHook(const char* reason);
};
