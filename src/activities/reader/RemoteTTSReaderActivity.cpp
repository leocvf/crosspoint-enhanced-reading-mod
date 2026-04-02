#include "RemoteTTSReaderActivity.h"

#include "RemoteTTSStreamSequencer.h"

#include <HalDisplay.h>
#include <Logging.h>

#include <algorithm>

#include "RemoteTTSConstants.h"
#include "RemoteTTSPacketFieldReaders.h"
#include "fontIds.h"
#include <BluetoothManager.h>

void RemoteTTSReaderActivity::onEnter() {
  Activity::onEnter();
  LOG_INF("RTTS", "Entering Remote TTS Reader mode");
  state.active = true;
  setDemoContent();
  lastCommandSummary = "none";
  commandCount = 0;
  viewportFirstLine = 0;
  autoFollowHighlight = true;
  lastRenderWasFullRefresh = true;
  lastRenderMs = 0;

  resetStreamingSession("onEnter");
  legacyMode.clear();
  hasPendingLegacyPosition = false;
  lastDocMismatchLogMs = 0;
  lastTelemetryLogMs = 0;
  lastLegacyPositionApplyMs = 0;

  const bool started = BluetoothManager::instance().start(X4_TTS_DEVICE_NAME,
                                                          [this](const std::string& payload) { handlePayload(payload); });
  if (!started) {
    LOG_ERR("RTTS", "BluetoothManager failed to start");
    setDebugMessage("BLE start failed", BluetoothManager::instance().getLastError());
  } else {
    setDebugMessage("BLE advertising", "Ready for commands");
  }
  lastConnectedState = BluetoothManager::instance().isConnected();
  lastAdvertisingState = BluetoothManager::instance().isAdvertising();
  
  // Force a clean background setup on first render
  state.textDirty = true;
  requestUpdate();
}

void RemoteTTSReaderActivity::onExit() {
  LOG_INF("RTTS", "Exiting Remote TTS Reader mode");
  BluetoothManager::instance().stop();
  if (renderer.hasBwBufferStored()) {
    renderer.restoreBwBuffer(); 
    renderer.displayBuffer(HalDisplay::HALF_REFRESH); // Clean exit blink
  }
  Activity::onExit();
}

void RemoteTTSReaderActivity::loop() {
  BluetoothManager::instance().poll();
  const BluetoothManager& bt = BluetoothManager::instance();
  const bool connected = bt.isConnected();
  const bool advertising = bt.isAdvertising();
  
  if (connected != lastConnectedState) {
    lastConnectedState = connected;
    if (connected) {
      setDebugMessage("BLE connected", streamMode ? "Streaming active" : "Waiting for text");
    } else {
      resetStreamingSession("disconnect");
      setDebugMessage("BLE disconnected", "Waiting for reconnect");
    }
  }

  if (!streamMode && legacyMode.tick(millis())) {
    state.currentDocId = legacyMode.docId();
    state.text = legacyMode.text();
    state.highlightStart = 0;
    state.highlightEnd = 0;
    state.textDirty = true;
    state.highlightDirty = true;
  }
  applyPendingLegacyPosition(millis());

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onExitToHome();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    autoFollowHighlight = !autoFollowHighlight;
    state.highlightDirty = true; // Refresh UI to show status change
  }

  const bool prevTriggered = mappedInput.wasPressed(MappedInputManager::Button::PageBack) ||
                             mappedInput.wasPressed(MappedInputManager::Button::Left);
  const bool nextTriggered = mappedInput.wasPressed(MappedInputManager::Button::PageForward) ||
                             mappedInput.wasPressed(MappedInputManager::Button::Right);

  if (prevTriggered) {
    autoFollowHighlight = false;
    viewportFirstLine = std::max(0, viewportFirstLine - 3);
    state.textDirty = true; // Viewport moved, need full re-render
    requestUpdate();
  } else if (nextTriggered) {
    autoFollowHighlight = false;
    viewportFirstLine += 3;
    state.textDirty = true; // Viewport moved, need full re-render
    requestUpdate();
  }

  if (state.textDirty || state.highlightDirty) {
    requestUpdate();
  }
}

void RemoteTTSReaderActivity::render(Activity::RenderLock&&) {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int margin = 16;
  const int contentWidth = screenWidth - (margin * 2);
  const int smallLineHeight = renderer.getLineHeight(SMALL_FONT_ID) + 2;
  const int fontId = BOOKERLY_18_FONT_ID;

  const bool needsFullRedraw = state.textDirty || !renderer.hasBwBufferStored() || consecutiveFastRefreshes >= 100;

  if (state.textDirty) {
    wrapText(contentWidth);
  }

  if (needsFullRedraw) {
    renderer.clearScreen();

    // Top Status Header - Static part
    renderer.drawText(UI_12_FONT_ID, margin, 4, "Stream Reader", true, EpdFontFamily::BOLD);
    
    // Health Bar Outline
    renderer.drawRect(margin, 22, contentWidth, 4, true);

    const int bodyLineHeight = renderer.getLineHeight(fontId) + 4;
    const int startY = 22 + 8 + smallLineHeight + 4;
    const int availableHeight = (screenHeight - 24) - startY;
    const int maxVisibleLines = std::max(1, availableHeight / bodyLineHeight);

    // Calculate viewport but DON'T draw highlights yet
    int preferredLine = viewportFirstLine;
    if (autoFollowHighlight && !wrappedLines.empty()) {
      for (size_t i = 0; i < wrappedLines.size(); ++i) {
        const auto& line = wrappedLines[i];
        if (!(state.highlightEnd <= line.start || state.highlightStart >= line.end)) {
          preferredLine = static_cast<int>(i) - (maxVisibleLines / 3);
          break;
        }
      }
    }
    const int maxFirstLine = std::max(0, static_cast<int>(wrappedLines.size()) - maxVisibleLines);
    viewportFirstLine = std::clamp(preferredLine, 0, maxFirstLine);

    // Draw clean text (no highlights)
    int y = startY;
    for (int i = viewportFirstLine; i < static_cast<int>(wrappedLines.size()); ++i) {
      const auto& line = wrappedLines[i];
      if (y + bodyLineHeight >= screenHeight - 22) break;
      renderer.drawText(fontId, margin, y, line.text.c_str(), true);
      y += bodyLineHeight;
    }

    // Capture this clean background
    renderer.storeBwBuffer();
    consecutiveFastRefreshes = 0;
  } else {
    // Fast path: restore the clean text background
    renderer.restoreBwBufferKeep();
  }

  // Now draw ONLY the dynamic parts on top of the clean background
  const BluetoothManager& bt = BluetoothManager::instance();
  std::string connStatus = bt.isConnected() ? "LINK OK" : "LINK LOST";
  renderer.drawText(SMALL_FONT_ID, screenWidth - margin - renderer.getTextWidth(SMALL_FONT_ID, connStatus.c_str()), 6, connStatus.c_str(), true);

  if (stats.lastBufferFillPct > 0) {
    int fillW = (contentWidth * std::min<uint32_t>(100, stats.lastBufferFillPct)) / 100;
    renderer.fillRect(margin, 22, fillW, 4, true);
  }

  int statusY = 22 + 8;
  char buf[64];
  snprintf(buf, sizeof(buf), "Buf: %u%% | Chunks: %u", stats.lastBufferFillPct, stats.chunksReceived);
  renderer.drawText(SMALL_FONT_ID, margin, statusY, buf, true);

  const int bodyLineHeight = renderer.getLineHeight(fontId) + 4;
  const int startY = statusY + smallLineHeight + 4;
  const int availableHeight = (screenHeight - 24) - startY;
  const int maxVisibleLines = std::max(1, availableHeight / bodyLineHeight);

  // Render Highlight
  int y = startY;
  for (int i = viewportFirstLine; i < static_cast<int>(wrappedLines.size()); ++i) {
    const auto& line = wrappedLines[i];
    if (y + bodyLineHeight >= screenHeight - 22) break;

    if (!(state.highlightEnd <= line.start || state.highlightStart >= line.end)) {
      renderer.fillRectDither(margin - 2, y - 1, contentWidth + 4, bodyLineHeight + 1, DarkGray);
      renderer.drawText(fontId, margin, y, line.text.c_str(), true);
    }
    y += bodyLineHeight;
  }

  snprintf(buf, sizeof(buf), "Page %d | %s", (viewportFirstLine / 5) + 1, streamMode ? "STREAM" : "LEGACY");
  renderer.drawText(SMALL_FONT_ID, margin, screenHeight - 12, buf, true);

  // True Partial Refresh
  if (!needsFullRedraw) {
    renderer.displayHighlightBuffer(); // FAST_REFRESH
    consecutiveFastRefreshes++;
  } else {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }

  lastRenderMs = millis();
  state.textDirty = false;
  state.highlightDirty = false;
}

void RemoteTTSReaderActivity::handlePayload(const std::string& payload) {
  if (payload.empty()) return;
  if (frameParser.push(payload) == RemoteTTSFrameParser::PushResult::OVERFLOW) return;
  std::string frame;
  while (frameParser.popFrame(frame)) processJsonFrame(frame);
}

void RemoteTTSReaderActivity::processJsonFrame(const std::string& frame) {
  if (frame.size() > MAX_JSON_BYTES) return;
  JsonDocument doc;
  if (deserializeJson(doc, frame)) return;
  handleCommand(doc);
}

void RemoteTTSReaderActivity::handleCommand(const JsonDocument& doc) {
  if (!doc["type"].is<const char*>()) return;
  const std::string type = doc["type"].as<const char*>();
  if (type == "clear") {
    clearLoadedContent();
    legacyMode.clear();
    resetStreamingSession("clear");
    state.textDirty = true;
    return;
  }
  if (type == "load_text") {
    if (!doc["docId"].is<const char*>() || !doc["text"].is<const char*>()) return;
    streamMode = false;
    legacyMode.onLoadText(doc["docId"].as<const char*>(), doc["text"].as<const char*>(), millis());
    if (legacyMode.consumeDocReadyDirty()) {
      state.currentDocId = legacyMode.docId();
      state.text = legacyMode.text();
      state.textDirty = true;
    }
    return;
  }
  if (type == "position") {
    if (!doc["docId"].is<const char*>() || !doc["start"].is<int>() || !doc["end"].is<int>()) return;
    int start = doc["start"].as<int>(), end = doc["end"].as<int>();
    if (streamMode) {
      if (start > end) std::swap(start, end);
      mapHighlightToRenderWindow(start, end);
      return;
    }
    if (!legacyMode.onPosition(doc["docId"].as<const char*>(), start, end)) return;
    pendingLegacyStart = legacyMode.highlightStart();
    pendingLegacyEnd = legacyMode.highlightEnd();
    hasPendingLegacyPosition = true;
    applyPendingLegacyPosition(millis(), true);
    return;
  }
  if (type == "stream_start") { handleStreamStart(doc); return; }
  if (type == "stream_chunk") { handleStreamChunk(doc); return; }
  if (type == "stream_commit") { handleStreamCommit(doc); return; }
  if (type == "stream_seek") { handleStreamSeek(doc); return; }
}

void RemoteTTSReaderActivity::applyPendingLegacyPosition(unsigned long nowMs, bool force) {
  if (!hasPendingLegacyPosition) return;
  state.highlightStart = pendingLegacyStart;
  state.highlightEnd = pendingLegacyEnd;
  state.highlightDirty = true;
  hasPendingLegacyPosition = false;
}

void RemoteTTSReaderActivity::handleStreamStart(const JsonDocument& doc) {
  if (!doc["sessionId"].is<const char*>()) return;
  streamMode = true;
  streamSessionId = doc["sessionId"].as<const char*>();
  committedText.clear();
  pendingChunks.clear();
  seenChunkSeq.clear();
  state.text.clear();
  state.textDirty = true;
}

void RemoteTTSReaderActivity::handleStreamChunk(const JsonDocument& doc) {
  uint32_t seq = 0; int offset = 0;
  if (!RemoteTTSPacketFieldReaders::readUIntAlias(doc, "seq", "sequenceId", seq)) return;
  const char* chunkText = doc["text"].as<const char*>();
  if (!chunkText || seenChunkSeq.count(seq) || pendingChunks.count(seq)) return;
  StreamChunk chunk; chunk.seq = seq; chunk.text = chunkText; chunk.offset = doc["offset"].as<int>();
  pendingChunks[seq] = chunk;
  stats.chunksReceived++;
  enforceStreamMemoryBudget();
  state.highlightDirty = true; // Update health bar only
}

void RemoteTTSReaderActivity::handleStreamCommit(const JsonDocument& doc) {
  uint32_t uptoSeq = 0;
  if (!RemoteTTSPacketFieldReaders::readUIntAlias(doc, "uptoSeq", "committedSeq", uptoSeq)) return;
  uint32_t seq = RemoteTTSStreamSequencer::nextExpectedSeq(highestContiguousSeq);
  bool changed = false;
  while (seq <= uptoSeq) {
    auto it = pendingChunks.find(seq);
    if (it == pendingChunks.end()) break;
    commitChunk(it->second);
    seenChunkSeq.insert(seq);
    pendingChunks.erase(it);
    highestContiguousSeq = seq++;
    changed = true;
  }
  if (changed) {
    state.text = committedText;
    state.renderWindowEnd = committedBaseOffset + committedText.size();
    state.highlightDirty = true; 
  }
}

void RemoteTTSReaderActivity::handleStreamSeek(const JsonDocument& doc) {
  int offset = 0;
  if (!RemoteTTSPacketFieldReaders::readIntAlias(doc, "offset", "start", offset)) return;
  renderPointerGlobal = offset;
  state.textDirty = true;
}

void RemoteTTSReaderActivity::commitChunk(const StreamChunk& chunk) {
  if (committedText.empty()) committedBaseOffset = chunk.offset;
  committedText += chunk.text;
  enforceStreamMemoryBudget();
}

void RemoteTTSReaderActivity::enforceStreamMemoryBudget() {
  while (committedText.size() > MAX_COMMITTED_BYTES) {
    committedText.erase(0, 256);
    committedBaseOffset += 256;
  }
}

void RemoteTTSReaderActivity::rebuildRenderWindow() { state.text = committedText; state.textDirty = true; }

void RemoteTTSReaderActivity::mapHighlightToRenderWindow(int globalStart, int globalEnd) {
  const int windowStart = committedBaseOffset;
  state.highlightStart = std::max(0, globalStart - windowStart);
  state.highlightEnd = std::max(state.highlightStart, globalEnd - windowStart);
  state.highlightDirty = true;
}

void RemoteTTSReaderActivity::emitAckLogHook(const char* reason) {
  JsonDocument ackDoc; ackDoc["type"] = "ack"; ackDoc["sessionId"] = streamSessionId;
  std::string ackPayload; serializeJson(ackDoc, ackPayload);
  BluetoothManager::instance().sendFeedback(ackPayload);
}

void RemoteTTSReaderActivity::resetStreamingSession(const std::string& reason) {
  streamMode = false; committedText.clear(); pendingChunks.clear(); highestContiguousSeq = -1;
}

void RemoteTTSReaderActivity::wrapText(int maxWidth) {
  wrappedLines.clear();
  const int fontId = BOOKERLY_18_FONT_ID;
  if (state.text.empty()) {
    wrappedLines.push_back({"Ready. Please send text from phone.", 0, 0});
    return;
  }
  std::string line;
  int lineStart = 0, idx = 0, wordStart = 0;
  while (idx <= static_cast<int>(state.text.size())) {
    const bool atEnd = idx == static_cast<int>(state.text.size());
    const bool separator = atEnd || state.text[idx] == ' ' || state.text[idx] == '\n';
    if (!separator) { idx++; continue; }
    std::string token = state.text.substr(wordStart, idx - wordStart);
    std::string candidate = line.empty() ? token : line + " " + token;
    if (!token.empty() && renderer.getTextWidth(fontId, candidate.c_str()) <= maxWidth) { line = candidate; }
    else {
      if (!line.empty()) wrappedLines.push_back({line, lineStart, wordStart - 1});
      line = token; lineStart = wordStart;
    }
    if (!atEnd && state.text[idx] == '\n') { wrappedLines.push_back({line, lineStart, idx}); line.clear(); lineStart = idx + 1; }
    idx++; wordStart = idx;
  }
  if (!line.empty()) wrappedLines.push_back({line, lineStart, static_cast<int>(state.text.size())});
}

void RemoteTTSReaderActivity::setDemoContent() { state.text.clear(); state.textDirty = true; }
void RemoteTTSReaderActivity::clearLoadedContent() { state.text.clear(); state.textDirty = true; }
void RemoteTTSReaderActivity::setDebugMessage(const std::string& line1, const std::string& line2) { debugLine1 = line1; debugLine2 = line2; state.highlightDirty = true; }
