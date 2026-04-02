#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

class RemoteTTSLegacyMode {
 public:
  enum class SessionState : uint8_t { IDLE = 0, DOC_LOADING, DOC_READY, POSITIONING, ERROR_RECOVERABLE };

  struct Counters {
    uint32_t parseErrors = 0;
    uint32_t docIdMismatch = 0;
    uint32_t outOfRangePosition = 0;
    uint32_t truncatedPayload = 0;
    uint32_t droppedFrames = 0;
  };

  struct Snapshot {
    SessionState state = SessionState::IDLE;
    const std::string* activeDocId = nullptr;
    size_t docLen = 0;
    uint32_t globalBaseOffset = 0;
    int start = 0;
    int end = 0;
    Counters counters;
  };

  static constexpr size_t MAX_DOC_BYTES = 32 * 1024;
  static constexpr size_t EVICT_CHUNK_BYTES = 4 * 1024;
  static constexpr unsigned long DEFAULT_FINALIZE_MS = 300;

  explicit RemoteTTSLegacyMode(unsigned long finalizeMs = DEFAULT_FINALIZE_MS) : finalizeDelayMs(finalizeMs) {}

  void clear() {
    state = SessionState::IDLE;
    activeDocId.clear();
    activeText.clear();
    pendingDocId.clear();
    pendingText.clear();
    globalBaseOffset = 0;
    pendingBaseOffset = 0;
    lastPositionStart = 0;
    lastPositionEnd = 0;
    hasPendingFinalize = false;
    docReadyDirty = true;
    highlightDirty = true;
  }

  bool onLoadText(const std::string& docId, const std::string& text, unsigned long nowMs) {
    if (docId.empty()) return false;

    // Reset if it's a completely new document
    if (!pendingDocId.empty() && pendingDocId != docId) {
      clear();
    }

    pendingDocId = docId;
    pendingText += text;

    // Sliding window enforcement: if pending text is too big, drop the start
    while (pendingText.size() > MAX_DOC_BYTES) {
      pendingText.erase(0, EVICT_CHUNK_BYTES);
      pendingBaseOffset += EVICT_CHUNK_BYTES;
    }

    hasPendingFinalize = true;
    finalizeAtMs = nowMs + finalizeDelayMs;
    state = SessionState::DOC_LOADING;
    return true;
  }

  bool onPosition(const std::string& docId, int globalStart, int globalEnd) {
    if (activeDocId.empty() || (state != SessionState::DOC_READY && state != SessionState::POSITIONING)) {
      return false;
    }
    if (docId != activeDocId) {
      counters.docIdMismatch++;
      return false;
    }

    // Map global book offsets to our local sliding window
    int localStart = static_cast<int>(globalStart) - static_cast<int>(globalBaseOffset);
    int localEnd = static_cast<int>(globalEnd) - static_cast<int>(globalBaseOffset);

    const int len = static_cast<int>(activeText.size());
    localStart = std::clamp(localStart, 0, len);
    localEnd = std::clamp(localEnd, 0, len);

    if (localStart > localEnd) std::swap(localStart, localEnd);
    
    // Ensure at least 1 char is highlighted if it's within our window
    if (localStart == localEnd && localStart < len) localEnd++;

    if (localStart == lastPositionStart && localEnd == lastPositionEnd) {
      return false;
    }

    lastPositionStart = localStart;
    lastPositionEnd = localEnd;
    state = SessionState::POSITIONING;
    highlightDirty = true;
    return true;
  }

  bool tick(unsigned long nowMs) {
    if (hasPendingFinalize && nowMs >= finalizeAtMs) {
      finalizePendingDoc();
      return true;
    }
    return false;
  }

  Snapshot snapshot() const {
    return Snapshot{state, &activeDocId, activeText.size(), globalBaseOffset, lastPositionStart, lastPositionEnd, counters};
  }

  const std::string& docId() const { return activeDocId; }
  const std::string& text() const { return activeText; }
  uint32_t getGlobalBaseOffset() const { return globalBaseOffset; }
  int highlightStart() const { return lastPositionStart; }
  int highlightEnd() const { return lastPositionEnd; }
  
  bool consumeDocReadyDirty() {
    const bool dirty = docReadyDirty;
    docReadyDirty = false;
    return dirty;
  }
  
  bool consumeHighlightDirty() {
    const bool dirty = highlightDirty;
    highlightDirty = false;
    return dirty;
  }

  Counters& mutableCounters() { return counters; }

 private:
  void finalizePendingDoc() {
    if (!hasPendingFinalize) return;
    activeDocId = pendingDocId;
    activeText = pendingText;
    globalBaseOffset = pendingBaseOffset;
    hasPendingFinalize = false;
    state = SessionState::DOC_READY;
    docReadyDirty = true;
    highlightDirty = true;
  }

  SessionState state = SessionState::IDLE;
  std::string activeDocId;
  std::string activeText;
  uint32_t globalBaseOffset = 0;

  std::string pendingDocId;
  std::string pendingText;
  uint32_t pendingBaseOffset = 0;

  int lastPositionStart = 0;
  int lastPositionEnd = 0;
  
  bool hasPendingFinalize = false;
  unsigned long finalizeAtMs = 0;
  unsigned long finalizeDelayMs = DEFAULT_FINALIZE_MS;
  
  bool docReadyDirty = true;
  bool highlightDirty = true;
  Counters counters;
};
