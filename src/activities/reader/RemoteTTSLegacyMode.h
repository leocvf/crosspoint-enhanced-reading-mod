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
    int start = 0;
    int end = 0;
    Counters counters;
  };

  static constexpr size_t MAX_DOC_BYTES = 32 * 1024;
  static constexpr unsigned long DEFAULT_FINALIZE_MS = 500;

  explicit RemoteTTSLegacyMode(unsigned long finalizeMs = DEFAULT_FINALIZE_MS) : finalizeDelayMs(finalizeMs) {}

  void clear() {
    state = SessionState::IDLE;
    activeDocId.clear();
    activeText.clear();
    pendingDocId.clear();
    pendingText.clear();
    lastPositionStart = 0;
    lastPositionEnd = 0;
    hasPendingFinalize = false;
    docReadyDirty = true;
    highlightDirty = true;
  }

  bool onLoadText(const std::string& docId, const std::string& text, unsigned long nowMs) {
    std::string sanitized;
    const bool hadInvalidUtf8 = !sanitizeUtf8(text, sanitized);
    if (hadInvalidUtf8) {
      counters.truncatedPayload++;
      state = SessionState::ERROR_RECOVERABLE;
    }

    if (!pendingDocId.empty() && pendingDocId != docId) {
      finalizePendingDoc();
    }

    if (pendingDocId != docId) {
      pendingDocId = docId;
      pendingText.clear();
    }

    if (pendingText.size() < MAX_DOC_BYTES) {
      const size_t available = MAX_DOC_BYTES - pendingText.size();
      if (sanitized.size() > available) {
        pendingText.append(sanitized.data(), available);
        counters.truncatedPayload++;
      } else {
        pendingText += sanitized;
      }
    } else {
      counters.truncatedPayload++;
    }

    hasPendingFinalize = true;
    finalizeAtMs = nowMs + finalizeDelayMs;
    state = SessionState::DOC_LOADING;
    return true;
  }

  bool onPosition(const std::string& docId, int start, int end) {
    if (activeDocId.empty() || (state != SessionState::DOC_READY && state != SessionState::POSITIONING)) {
      return false;
    }
    if (docId != activeDocId) {
      counters.docIdMismatch++;
      return false;
    }

    const int len = static_cast<int>(activeText.size());
    const int rawStart = start;
    const int rawEnd = end;

    start = std::clamp(start, 0, len);
    end = std::clamp(end, 0, len);
    if (start > end) {
      std::swap(start, end);
    }
    if (start == end) {
      if (end < len) {
        end += 1;
      } else if (start > 0) {
        start -= 1;
      }
    }

    if (start != rawStart || end != rawEnd) {
      counters.outOfRangePosition++;
    }

    if (start == lastPositionStart && end == lastPositionEnd) {
      return false;
    }

    lastPositionStart = start;
    lastPositionEnd = end;
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
    return Snapshot{state, &activeDocId, activeText.size(), lastPositionStart, lastPositionEnd, counters};
  }

  const std::string& docId() const { return activeDocId; }
  const std::string& text() const { return activeText; }
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
  static bool sanitizeUtf8(const std::string& input, std::string& out) {
    out.clear();
    out.reserve(input.size());
    bool clean = true;

    for (size_t i = 0; i < input.size();) {
      const unsigned char c = static_cast<unsigned char>(input[i]);
      if (c <= 0x7F) {
        out.push_back(static_cast<char>(c));
        i++;
        continue;
      }

      size_t need = 0;
      if ((c & 0xE0) == 0xC0) need = 2;
      else if ((c & 0xF0) == 0xE0) need = 3;
      else if ((c & 0xF8) == 0xF0) need = 4;
      else {
        clean = false;
        i++;
        continue;
      }

      if (i + need > input.size()) {
        clean = false;
        break;
      }

      bool valid = true;
      for (size_t j = 1; j < need; ++j) {
        const unsigned char cc = static_cast<unsigned char>(input[i + j]);
        if ((cc & 0xC0) != 0x80) {
          valid = false;
          break;
        }
      }

      if (!valid) {
        clean = false;
        i++;
        continue;
      }

      out.append(input, i, need);
      i += need;
    }

    return clean;
  }

  void finalizePendingDoc() {
    if (!hasPendingFinalize) {
      return;
    }
    activeDocId = pendingDocId;
    activeText = pendingText;
    hasPendingFinalize = false;
    state = SessionState::DOC_READY;
    lastPositionStart = 0;
    lastPositionEnd = 0;
    docReadyDirty = true;
    highlightDirty = true;
  }

  SessionState state = SessionState::IDLE;
  std::string activeDocId;
  std::string activeText;
  std::string pendingDocId;
  std::string pendingText;
  int lastPositionStart = 0;
  int lastPositionEnd = 0;
  bool hasPendingFinalize = false;
  unsigned long finalizeAtMs = 0;
  unsigned long finalizeDelayMs = DEFAULT_FINALIZE_MS;
  bool docReadyDirty = true;
  bool highlightDirty = true;
  Counters counters;
};
