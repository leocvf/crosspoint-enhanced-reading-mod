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
  requestUpdate();
}

void RemoteTTSReaderActivity::onExit() {
  LOG_INF("RTTS", "Exiting Remote TTS Reader mode");
  BluetoothManager::instance().stop();
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
      setDebugMessage("BLE connected", streamMode ? "Streaming active" : "Waiting for load_text");
    } else {
      resetStreamingSession("disconnect");
      setDebugMessage("BLE disconnected", "Session reset; ready for reconnect");
    }
  }
  if (advertising != lastAdvertisingState) {
    lastAdvertisingState = advertising;
    if (!connected) {
      if (advertising) {
        setDebugMessage("BLE advertising", "Ready for commands");
      } else {
        setDebugMessage("BLE stopped", "Retrying advertising...");
      }
    } else {
      requestUpdate();
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
  logLegacyTelemetryThrottled(millis());

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onExitToHome();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    autoFollowHighlight = !autoFollowHighlight;
    setDebugMessage(autoFollowHighlight ? "Auto-follow enabled" : "Auto-follow paused");
  }

  const bool prevTriggered = mappedInput.wasPressed(MappedInputManager::Button::PageBack) ||
                             mappedInput.wasPressed(MappedInputManager::Button::Left);
  const bool nextTriggered = mappedInput.wasPressed(MappedInputManager::Button::PageForward) ||
                             mappedInput.wasPressed(MappedInputManager::Button::Right);

  if (prevTriggered) {
    autoFollowHighlight = false;
    viewportFirstLine = std::max(0, viewportFirstLine - 3);
    requestUpdate();
  } else if (nextTriggered) {
    autoFollowHighlight = false;
    viewportFirstLine += 3;
    requestUpdate();
  }

  if (state.textDirty || state.highlightDirty) {
    const unsigned long now = millis();
    const bool onlyHighlightDirty = state.highlightDirty && !state.textDirty;
    const unsigned long minRenderIntervalMs = onlyHighlightDirty ? RENDER_COALESCE_MS : 40;
    if ((now - lastRenderMs) >= minRenderIntervalMs) {
      requestUpdate();
    }
  }
}

void RemoteTTSReaderActivity::render(Activity::RenderLock&&) {
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int margin = 16;
  const int contentWidth = screenWidth - (margin * 2);
  const int smallLineHeight = renderer.getLineHeight(SMALL_FONT_ID) + 2;

  if (state.textDirty) {
    wrapText(contentWidth);
  }

  renderer.clearScreen();
  renderer.drawText(UI_12_FONT_ID, margin, 6, "Remote TTS Reader", true, EpdFontFamily::BOLD);
  const BluetoothManager& bt = BluetoothManager::instance();
  const char* bleState = bt.isConnected() ? "Connected" : (bt.isAdvertising() ? "Advertising" : "Stopped");
  int statusY = 20;
  const int bodyLineHeight = renderer.getLineHeight(UI_10_FONT_ID) + 2;
  int statusLineBudget = 7;
  auto drawWrappedSmall = [&](const std::string& text) {
    if (text.empty() || statusLineBudget <= 0) {
      return;
    }

    std::string line;
    size_t idx = 0;
    while (idx < text.size()) {
      while (idx < text.size() && text[idx] == ' ') {
        idx++;
      }
      size_t wordEnd = idx;
      while (wordEnd < text.size() && text[wordEnd] != ' ') {
        wordEnd++;
      }
      const std::string word = text.substr(idx, wordEnd - idx);
      const std::string candidate = line.empty() ? word : (line + " " + word);
      const bool fits = renderer.getTextWidth(SMALL_FONT_ID, candidate.c_str()) <= contentWidth;

      if (!line.empty() && !fits) {
        if (statusLineBudget <= 0) {
          return;
        }
        renderer.drawText(SMALL_FONT_ID, margin, statusY, line.c_str(), true);
        statusY += smallLineHeight;
        statusLineBudget--;
        line = word;
      } else {
        line = candidate;
      }

      idx = wordEnd;
    }

    if (!line.empty() && statusLineBudget > 0) {
      renderer.drawText(SMALL_FONT_ID, margin, statusY, line.c_str(), true);
      statusY += smallLineHeight;
      statusLineBudget--;
    }
  };

  drawWrappedSmall(std::string("BLE: ") + bleState);
  drawWrappedSmall(std::string("Device: ") + X4_TTS_DEVICE_NAME);
  drawWrappedSmall(streamMode ? ("Session: " + streamSessionId) : "Session: legacy");
  drawWrappedSmall(debugLine1);
  if (!debugLine2.empty()) {
    drawWrappedSmall(debugLine2);
  } else if (!bt.getLastError().empty()) {
    drawWrappedSmall(bt.getLastError());
  }
  drawWrappedSmall("Cmds: " + std::to_string(commandCount) + "  Last: " + lastCommandSummary);
  drawWrappedSmall("Chunks " + std::to_string(stats.chunksReceived) + "/" +
                   std::to_string(stats.duplicateChunks) + "/" + std::to_string(stats.gapEvents) +
                   "  Miss: " + std::to_string(stats.highlightMisses));

  const int minTextLines = 3;
  const int maxStatusBottom = (screenHeight - 18) - (minTextLines * bodyLineHeight) - 6;
  if (statusY > maxStatusBottom) {
    statusY = std::max(20, maxStatusBottom);
  }
  const int startY = statusY + 6;

  const int lineHeight = bodyLineHeight;
  const int availableHeight = (screenHeight - 18) - startY;
  const int maxVisibleLines = std::max(1, availableHeight / lineHeight);

  int preferredLine = viewportFirstLine;
  if (autoFollowHighlight && !wrappedLines.empty()) {
    for (size_t i = 0; i < wrappedLines.size(); ++i) {
      const auto& line = wrappedLines[i];
      const bool intersects = !(state.highlightEnd <= line.start || state.highlightStart >= line.end);
      if (intersects) {
        preferredLine = static_cast<int>(i) - (maxVisibleLines / 2);
        break;
      }
    }
  }

  const int maxFirstLine = std::max(0, static_cast<int>(wrappedLines.size()) - maxVisibleLines);
  viewportFirstLine = std::clamp(preferredLine, 0, maxFirstLine);

  int y = startY;
  for (int i = viewportFirstLine; i < static_cast<int>(wrappedLines.size()); ++i) {
    const auto& line = wrappedLines[i];
    if (y + lineHeight >= screenHeight - 18) {
      break;
    }

    const bool intersects = !(state.highlightEnd <= line.start || state.highlightStart >= line.end);
    if (intersects) {
      renderer.fillRect(margin - 2, y - 1, contentWidth + 4, lineHeight + 1, false);
      renderer.drawText(UI_10_FONT_ID, margin, y, line.text.c_str(), false);
    } else {
      renderer.drawText(UI_10_FONT_ID, margin, y, line.text.c_str(), true);
    }

    y += lineHeight;
  }

  const int totalPages = std::max(1, (static_cast<int>(wrappedLines.size()) + maxVisibleLines - 1) / maxVisibleLines);
  const int currentPage = std::min(totalPages, (viewportFirstLine / maxVisibleLines) + 1);
  renderer.drawText(SMALL_FONT_ID, margin, screenHeight - 20,
                    ("View " + std::to_string(currentPage) + "/" + std::to_string(totalPages)).c_str());
  renderer.drawText(SMALL_FONT_ID, margin, screenHeight - 10, "OK: Follow  Pg +/-: Scroll  Back: Home");

  const unsigned long now = millis();
  const bool onlyHighlightDirty = state.highlightDirty && !state.textDirty;
  const bool shouldUseFastRefresh = onlyHighlightDirty && (now - lastRenderMs) >= RENDER_COALESCE_MS;
  renderer.displayBuffer(shouldUseFastRefresh ? HalDisplay::FAST_REFRESH : HalDisplay::HALF_REFRESH);
  lastRenderMs = now;
  lastRenderWasFullRefresh = !shouldUseFastRefresh;

  state.textDirty = false;
  state.highlightDirty = false;
}

void RemoteTTSReaderActivity::handlePayload(const std::string& payload) {
  if (payload.empty()) {
    return;
  }

  if (frameParser.push(payload) == RemoteTTSFrameParser::PushResult::OVERFLOW) {
    legacyMode.mutableCounters().truncatedPayload++;
    legacyMode.mutableCounters().droppedFrames++;
    return;
  }

  std::string frame;
  while (frameParser.popFrame(frame)) {
    processJsonFrame(frame);
  }
}

void RemoteTTSReaderActivity::processJsonFrame(const std::string& frame) {
  if (frame.size() > MAX_JSON_BYTES) {
    legacyMode.mutableCounters().truncatedPayload++;
    legacyMode.mutableCounters().droppedFrames++;
    return;
  }

  JsonDocument doc;
  auto err = deserializeJson(doc, frame);
  if (err) {
    stats.malformedPackets++;
    legacyMode.mutableCounters().parseErrors++;
    legacyMode.mutableCounters().droppedFrames++;
    return;
  }

  handleCommand(doc);
}

void RemoteTTSReaderActivity::handleCommand(const JsonDocument& doc) {
  if (!doc["type"].is<const char*>()) {
    stats.malformedPackets++;
    LOG_ERR("RTTS", "Rejected command: missing type");
    setDebugMessage("Rejected command", "Missing type");
    return;
  }

  const std::string type = doc["type"].as<const char*>();
  commandCount++;
  lastCommandSummary = type;

  if (type == "ping") {
    LOG_INF("RTTS", "Received ping");
    setDebugMessage("Command: ping");
    return;
  }

  if (type == "clear") {
    clearLoadedContent();
    legacyMode.clear();
    resetStreamingSession("clear");
    LOG_INF("RTTS", "Cleared document state");
    setDebugMessage("Command: clear");
    return;
  }

  if (type == "load_text") {
    if (!doc["docId"].is<const char*>() || !doc["text"].is<const char*>()) {
      stats.malformedPackets++;
      LOG_ERR("RTTS", "Rejected load_text: missing docId/text");
      setDebugMessage("Rejected load_text", "Missing docId/text");
      return;
    }

    streamMode = false;
    legacyMode.onLoadText(doc["docId"].as<const char*>(), doc["text"].as<const char*>(), millis());
    if (legacyMode.consumeDocReadyDirty()) {
      state.currentDocId = legacyMode.docId();
      state.text = legacyMode.text();
      state.highlightStart = 0;
      state.highlightEnd = 0;
      state.textDirty = true;
      state.highlightDirty = true;
      renderPointerGlobal = 0;
      LOG_INF("RTTS", "Loaded docId=%s chars=%d", state.currentDocId.c_str(), state.text.size());
      setDebugMessage("Loaded text", ("docId=" + state.currentDocId).c_str());
    }
    return;
  }

  if (type == "position") {
    if (!doc["docId"].is<const char*>() || !doc["start"].is<int>() || !doc["end"].is<int>()) {
      stats.malformedPackets++;
      LOG_ERR("RTTS", "Rejected position: missing fields");
      setDebugMessage("Rejected position", "Missing docId/start/end");
      return;
    }

    const std::string docId = doc["docId"].as<const char*>();
    int start = doc["start"].as<int>();
    int end = doc["end"].as<int>();

    if (streamMode) {
      if (start > end) {
        std::swap(start, end);
      }
      mapHighlightToRenderWindow(start, end);
      renderPointerGlobal = start;
      return;
    }

    const bool changed = legacyMode.onPosition(docId, start, end);
    if (!changed) {
      if (docId != state.currentDocId && (millis() - lastDocMismatchLogMs) >= 1000) {
        lastDocMismatchLogMs = millis();
        LOG_INF("RTTS", "Ignored position for docId=%s current=%s", docId.c_str(), state.currentDocId.c_str());
      }
      return;
    }

    pendingLegacyStart = legacyMode.highlightStart();
    pendingLegacyEnd = legacyMode.highlightEnd();
    hasPendingLegacyPosition = true;
    applyPendingLegacyPosition(millis(), true);
    return;
  }

  if (type == "stream_start") {
    handleStreamStart(doc);
    return;
  }

  if (type == "stream_chunk") {
    handleStreamChunk(doc);
    return;
  }

  if (type == "stream_commit") {
    handleStreamCommit(doc);
    return;
  }

  if (type == "stream_seek") {
    handleStreamSeek(doc);
    return;
  }

  if (type == "stream_end") {
    handleStreamEnd(doc);
    return;
  }

  LOG_ERR("RTTS", "Rejected command: unknown type %s", type.c_str());
  setDebugMessage("Rejected command", ("Unknown type: " + type).c_str());
}

void RemoteTTSReaderActivity::applyPendingLegacyPosition(unsigned long nowMs, bool force) {
  if (!hasPendingLegacyPosition) {
    return;
  }
  if (!force && (nowMs - lastLegacyPositionApplyMs) < LEGACY_POSITION_RENDER_INTERVAL_MS) {
    return;
  }
  lastLegacyPositionApplyMs = nowMs;
  state.highlightStart = pendingLegacyStart;
  state.highlightEnd = pendingLegacyEnd;
  state.highlightDirty = true;
  hasPendingLegacyPosition = false;
  renderPointerGlobal = state.highlightStart;
}

void RemoteTTSReaderActivity::logLegacyTelemetryThrottled(unsigned long nowMs) {
  if (nowMs - lastTelemetryLogMs < 1000) {
    return;
  }
  lastTelemetryLogMs = nowMs;
  const auto snap = legacyMode.snapshot();
  LOG_INF("RTTS", "legacy status activeDocId=%s docLen=%u readyState=%u lastPosition=%d-%d parseErr=%u drop=%u",
          snap.activeDocId->c_str(), static_cast<unsigned int>(snap.docLen), static_cast<unsigned int>(snap.state),
          snap.start, snap.end, static_cast<unsigned int>(snap.counters.parseErrors),
          static_cast<unsigned int>(snap.counters.droppedFrames));
}

void RemoteTTSReaderActivity::handleStreamStart(const JsonDocument& doc) {
  if (!doc["sessionId"].is<const char*>() || !doc["docId"].is<const char*>()) {
    stats.malformedPackets++;
    setDebugMessage("Rejected stream_start", "Missing sessionId/docId");
    return;
  }

  streamMode = true;
  streamSessionId = doc["sessionId"].as<const char*>();
  streamDocId = doc["docId"].as<const char*>();
  state.currentDocId = streamDocId;
  streamStartSeq = doc["startSeq"].is<uint32_t>() ? doc["startSeq"].as<uint32_t>() : 0;
  highestContiguousSeq = RemoteTTSStreamSequencer::initialHighestContiguousSeq(streamStartSeq);
  lastCommitSeq = highestContiguousSeq < 0 ? 0 : static_cast<uint32_t>(highestContiguousSeq);
  committedBaseOffset = 0;
  committedText.clear();
  deferredCommitted.clear();
  pendingChunks.clear();
  seenChunkSeq.clear();
  stats = {};
  renderPointerGlobal = 0;
  state.text.clear();
  state.textDirty = true;
  state.highlightDirty = true;
  renderPointerGlobal = std::max(0, doc["startOffset"].is<int>() ? doc["startOffset"].as<int>() : 0);
  LOG_INF("RTTS", "stream_start session=%s docId=%s startSeq=%u ver=%d totalChars=%d", streamSessionId.c_str(),
          streamDocId.c_str(), static_cast<unsigned int>(streamStartSeq),
          doc["streamVersion"].is<int>() ? doc["streamVersion"].as<int>() : 1,
          doc["totalChars"].is<int>() ? doc["totalChars"].as<int>() : -1);
  LOG_DBG("RTTS", "stream_start baseline startSeq=%u highestInit=%d firstCommitTarget=%u",
          static_cast<unsigned int>(streamStartSeq), static_cast<int>(highestContiguousSeq),
          static_cast<unsigned int>(RemoteTTSStreamSequencer::nextExpectedSeq(highestContiguousSeq)));
  setDebugMessage("stream_start", streamSessionId);
}

void RemoteTTSReaderActivity::handleStreamChunk(const JsonDocument& doc) {
  uint32_t seq = 0;
  int offset = 0;
  const bool hasSeq = RemoteTTSPacketFieldReaders::readUIntAlias(doc, "seq", "sequenceId", seq);
  const bool hasOffset = RemoteTTSPacketFieldReaders::readIntAlias(doc, "offset", "start", offset);
  if (!doc["sessionId"].is<const char*>() || !hasSeq || !hasOffset || !doc["text"].is<const char*>()) {
    stats.malformedPackets++;
    if (!hasSeq && !hasOffset) {
      setDebugMessage("Rejected stream_chunk", "Missing (seq|sequenceId) and (offset|start)");
    } else if (!hasSeq) {
      setDebugMessage("Rejected stream_chunk", "Missing seq/sequenceId");
    } else if (!hasOffset) {
      setDebugMessage("Rejected stream_chunk", "Missing offset/start");
    } else {
      setDebugMessage("Rejected stream_chunk", "Missing sessionId/text");
    }
    return;
  }

  const std::string session = doc["sessionId"].as<const char*>();
  if (!streamMode || session != streamSessionId) {
    LOG_INF("RTTS", "Ignored stream_chunk for inactive session=%s", session.c_str());
    return;
  }

  if (seenChunkSeq.find(seq) != seenChunkSeq.end() || pendingChunks.find(seq) != pendingChunks.end()) {
    stats.duplicateChunks++;
    emitAckLogHook("duplicate chunk");
    return;
  }

  StreamChunk chunk;
  chunk.seq = seq;
  chunk.offset = offset;
  chunk.text = doc["text"].as<const char*>();
  chunk.receivedAtMs = millis();
  if (doc["reason"].is<const char*>()) {
    LOG_DBG("RTTS", "Chunk seq=%u reason=%s", static_cast<unsigned int>(seq), doc["reason"].as<const char*>());
  }
  if (doc["checksum"].is<uint32_t>() || doc["checksum"].is<const char*>()) {
    LOG_DBG("RTTS", "Chunk seq=%u includes checksum metadata", static_cast<unsigned int>(seq));
  }

  pendingChunks[seq] = chunk;
  stats.chunksReceived++;
  if (pendingChunks.size() > MAX_PENDING_CHUNKS) {
    pendingChunks.erase(pendingChunks.begin());
    stats.gapEvents++;
  }

  enforceStreamMemoryBudget();
  emitAckLogHook("chunk");
}

void RemoteTTSReaderActivity::handleStreamCommit(const JsonDocument& doc) {
  uint32_t uptoSeq = 0;
  if (!doc["sessionId"].is<const char*>() ||
      !RemoteTTSPacketFieldReaders::readUIntAlias(doc, "uptoSeq", "committedSeq", uptoSeq)) {
    stats.malformedPackets++;
    setDebugMessage("Rejected stream_commit",
                    doc["sessionId"].is<const char*>() ? "Missing uptoSeq/committedSeq" : "Missing sessionId");
    return;
  }

  const std::string session = doc["sessionId"].as<const char*>();
  if (!streamMode || session != streamSessionId) {
    LOG_INF("RTTS", "Ignored stream_commit for inactive session=%s", session.c_str());
    return;
  }

  if (streamCommitStartedAtMs == 0) {
    streamCommitStartedAtMs = millis();
  }

  uint32_t seq = RemoteTTSStreamSequencer::nextExpectedSeq(highestContiguousSeq);
  while (seq <= uptoSeq) {
    auto it = pendingChunks.find(seq);
    if (it == pendingChunks.end()) {
      stats.gapEvents++;
      break;
    }
    commitChunk(it->second);
    seenChunkSeq.insert(seq);
    pendingChunks.erase(it);
    highestContiguousSeq = static_cast<int64_t>(seq);
    seq++;
  }

  lastCommitSeq = std::max(lastCommitSeq, uptoSeq);
  stats.commits++;
  const uint32_t commitLatency = millis() - streamCommitStartedAtMs;
  stats.commitLatencyMs = commitLatency;
  stats.maxCommitLatencyMs = std::max(stats.maxCommitLatencyMs, commitLatency);
  streamCommitStartedAtMs = 0;

  rebuildRenderWindow();
  emitAckLogHook("commit");
}

void RemoteTTSReaderActivity::handleStreamSeek(const JsonDocument& doc) {
  int offset = 0;
  const bool hasOffset = RemoteTTSPacketFieldReaders::readIntAlias(doc, "offset", "start", offset);
  if (!doc["sessionId"].is<const char*>() || !hasOffset) {
    stats.malformedPackets++;
    setDebugMessage("Rejected stream_seek",
                    doc["sessionId"].is<const char*>() ? "Missing offset/start" : "Missing sessionId");
    return;
  }

  const std::string session = doc["sessionId"].as<const char*>();
  if (!streamMode || session != streamSessionId) {
    LOG_INF("RTTS", "Ignored stream_seek for inactive session=%s", session.c_str());
    return;
  }

  renderPointerGlobal = std::max(0, offset);
  if (doc["resetSeq"].is<uint32_t>()) {
    const uint32_t resetSeq = doc["resetSeq"].as<uint32_t>();
    highestContiguousSeq = RemoteTTSStreamSequencer::initialHighestContiguousSeq(resetSeq);
    lastCommitSeq = highestContiguousSeq < 0 ? 0 : static_cast<uint32_t>(highestContiguousSeq);
    pendingChunks.clear();
    seenChunkSeq.clear();
    LOG_INF("RTTS", "stream_seek requested seq reset to %u", static_cast<unsigned int>(resetSeq));
  }
  rebuildRenderWindow();
  setDebugMessage("stream_seek", std::to_string(renderPointerGlobal));
}

void RemoteTTSReaderActivity::handleStreamEnd(const JsonDocument& doc) {
  if (!doc["sessionId"].is<const char*>()) {
    stats.malformedPackets++;
    setDebugMessage("Rejected stream_end", "Missing sessionId");
    return;
  }
  const std::string session = doc["sessionId"].as<const char*>();
  if (!streamMode || session != streamSessionId) {
    return;
  }

  emitAckLogHook("end");
  setDebugMessage("stream_end", "Session complete");
}

void RemoteTTSReaderActivity::commitChunk(const StreamChunk& chunk) {
  if (committedText.empty()) {
    committedBaseOffset = chunk.offset;
  }

  const int committedEnd = committedBaseOffset + static_cast<int>(committedText.size());
  if (chunk.offset > committedEnd) {
    deferredCommitted[chunk.offset] = chunk.text;
    stats.gapEvents++;
    return;
  }

  int trim = std::max(0, committedEnd - chunk.offset);
  if (trim >= static_cast<int>(chunk.text.size())) {
    return;
  }

  committedText += chunk.text.substr(trim);
  stitchDeferredCommitted();
  enforceStreamMemoryBudget();
}

void RemoteTTSReaderActivity::stitchDeferredCommitted() {
  while (!deferredCommitted.empty()) {
    auto it = deferredCommitted.begin();
    const int committedEnd = committedBaseOffset + static_cast<int>(committedText.size());
    if (it->first > committedEnd) {
      break;
    }

    int trim = std::max(0, committedEnd - it->first);
    if (trim < static_cast<int>(it->second.size())) {
      committedText += it->second.substr(trim);
    }
    deferredCommitted.erase(it);
  }
}

void RemoteTTSReaderActivity::enforceStreamMemoryBudget() {
  size_t pendingBytes = 0;
  for (const auto& kv : pendingChunks) {
    pendingBytes += kv.second.text.size();
  }

  while (committedText.size() > MAX_COMMITTED_BYTES || (pendingBytes + committedText.size()) > MAX_STREAM_BYTES) {
    const size_t evictBytes = std::min<size_t>(128, committedText.size());
    committedText.erase(0, evictBytes);
    committedBaseOffset += static_cast<int>(evictBytes);
    if (state.highlightStart < committedBaseOffset) {
      state.highlightStart = committedBaseOffset;
      state.highlightEnd = std::max(state.highlightEnd, state.highlightStart);
      state.highlightDirty = true;
    }
  }
}

void RemoteTTSReaderActivity::rebuildRenderWindow() {
  state.text = committedText;
  state.renderWindowStart = committedBaseOffset;
  state.renderWindowEnd = committedBaseOffset + static_cast<int>(committedText.size());
  state.textDirty = true;

  mapHighlightToRenderWindow(renderPointerGlobal, renderPointerGlobal + 1);
}

void RemoteTTSReaderActivity::mapHighlightToRenderWindow(int globalStart, int globalEnd) {
  if (globalStart > globalEnd) {
    std::swap(globalStart, globalEnd);
  }

  const int windowStart = state.renderWindowStart;
  const int windowEnd = state.renderWindowEnd;
  if (windowEnd <= windowStart) {
    state.highlightStart = 0;
    state.highlightEnd = 0;
    state.highlightDirty = true;
    return;
  }

  const int clampedStart = std::clamp(globalStart, windowStart, windowEnd);
  const int clampedEnd = std::clamp(globalEnd, windowStart, windowEnd);
  if (clampedStart != globalStart || clampedEnd != globalEnd) {
    stats.highlightMisses++;
    emitAckLogHook("highlight miss");
  }

  state.highlightStart = clampedStart - windowStart;
  state.highlightEnd = std::max(state.highlightStart, clampedEnd - windowStart);
  state.highlightDirty = true;
}

void RemoteTTSReaderActivity::emitAckLogHook(const char* reason) {
  uint32_t nextExpected = RemoteTTSStreamSequencer::nextExpectedSeq(highestContiguousSeq);
  std::string missing;
  const uint32_t maxProbe = std::min<uint32_t>(lastCommitSeq + 8, nextExpected + 8);
  for (uint32_t s = nextExpected; s <= maxProbe; ++s) {
    if (pendingChunks.find(s) == pendingChunks.end()) {
      if (!missing.empty()) {
        missing += ",";
      }
      missing += std::to_string(s);
    }
  }

  size_t pendingBytes = 0;
  for (const auto& kv : pendingChunks) {
    pendingBytes += kv.second.text.size();
  }
  const size_t fillPct = (100 * (pendingBytes + committedText.size())) / MAX_STREAM_BYTES;

  LOG_INF("RTTS", "ack_hook reason=%s session=%s highContig=%d missing=%s fill=%u%% commitMs=%u/%u",
          reason, streamSessionId.c_str(), static_cast<int>(highestContiguousSeq), missing.empty() ? "none" : missing.c_str(),
          static_cast<unsigned int>(fillPct), static_cast<unsigned int>(stats.commitLatencyMs),
          static_cast<unsigned int>(stats.maxCommitLatencyMs));

  JsonDocument ackDoc;
  ackDoc["type"] = "ack";
  ackDoc["sessionId"] = streamSessionId;
  ackDoc["reason"] = reason;
  const uint32_t ackSeq = highestContiguousSeq < 0 ? 0 : static_cast<uint32_t>(highestContiguousSeq);
  ackDoc["sequenceId"] = ackSeq;
  ackDoc["highestContiguousSeq"] = ackSeq;
  ackDoc["bufferFillPct"] = static_cast<unsigned int>(fillPct);
  ackDoc["missing"] = missing;
  ackDoc["commitLatencyMs"] = stats.commitLatencyMs;
  std::string ackPayload;
  serializeJson(ackDoc, ackPayload);
  if (!BluetoothManager::instance().sendFeedback(ackPayload)) {
    LOG_DBG("RTTS", "ACK notify unavailable; keeping log-only feedback");
  }
}

void RemoteTTSReaderActivity::resetStreamingSession(const std::string& reason) {
  streamMode = false;
  streamSessionId.clear();
  streamDocId.clear();
  highestContiguousSeq = -1;
  lastCommitSeq = 0;
  streamStartSeq = 1;
  committedBaseOffset = 0;
  committedText.clear();
  deferredCommitted.clear();
  pendingChunks.clear();
  seenChunkSeq.clear();
  streamCommitStartedAtMs = 0;
  renderPointerGlobal = 0;
  LOG_INF("RTTS", "Streaming session reset (%s)", reason.c_str());
}

void RemoteTTSReaderActivity::wrapText(int maxWidth) {
  wrappedLines.clear();
  if (state.text.empty()) {
    wrappedLines.push_back({"Ready to receive text from phone app.", 0, 0});
    wrappedLines.push_back({"1) Connect to BLE device", 0, 0});
    wrappedLines.push_back({"2) Send load_text or stream_start", 0, 0});
    wrappedLines.push_back({"3) Send position/stream_commit", 0, 0});
    return;
  }

  std::string line;
  int lineStart = 0;
  int idx = 0;
  int wordStart = 0;

  while (idx <= static_cast<int>(state.text.size())) {
    const bool atEnd = idx == static_cast<int>(state.text.size());
    const bool separator = atEnd || state.text[idx] == ' ' || state.text[idx] == '\n';

    if (!separator) {
      idx++;
      continue;
    }

    std::string token = state.text.substr(wordStart, idx - wordStart);
    std::string candidate = line.empty() ? token : line + " " + token;

    if (!token.empty() && renderer.getTextWidth(UI_10_FONT_ID, candidate.c_str()) <= maxWidth) {
      line = candidate;
    } else {
      if (!line.empty()) {
        wrappedLines.push_back({line, lineStart, wordStart - 1});
      }
      line = token;
      lineStart = wordStart;
    }

    if (!atEnd && state.text[idx] == '\n') {
      wrappedLines.push_back({line, lineStart, idx});
      line.clear();
      lineStart = idx + 1;
    }

    idx++;
    wordStart = idx;
  }

  if (!line.empty()) {
    wrappedLines.push_back({line, lineStart, static_cast<int>(state.text.size())});
  }
}

void RemoteTTSReaderActivity::setDemoContent() {
  state.currentDocId.clear();
  state.text.clear();
  state.highlightStart = 0;
  state.highlightEnd = 0;
  state.textDirty = true;
  state.highlightDirty = true;
  state.renderWindowStart = 0;
  state.renderWindowEnd = 0;
}

void RemoteTTSReaderActivity::clearLoadedContent() {
  state.currentDocId.clear();
  state.text.clear();
  state.highlightStart = 0;
  state.highlightEnd = 0;
  state.textDirty = true;
  state.highlightDirty = true;
  state.renderWindowStart = 0;
  state.renderWindowEnd = 0;
}

void RemoteTTSReaderActivity::setDebugMessage(const std::string& line1, const std::string& line2) {
  debugLine1 = line1;
  debugLine2 = line2;
  state.highlightDirty = true;
}
