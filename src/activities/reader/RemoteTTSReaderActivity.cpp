#include "RemoteTTSReaderActivity.h"

#include <HalDisplay.h>
#include <Logging.h>

#include <algorithm>

#include "RemoteTTSConstants.h"
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
      setDebugMessage("BLE connected", "Waiting for load_text");
    } else {
      setDebugMessage("BLE disconnected", "Ready for reconnect");
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
    requestUpdate();
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
  auto drawWrappedSmall = [&](const std::string& text) {
    if (text.empty()) {
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
        renderer.drawText(SMALL_FONT_ID, margin, statusY, line.c_str(), true);
        statusY += smallLineHeight;
        line = word;
      } else {
        line = candidate;
      }

      idx = wordEnd;
    }

    if (!line.empty()) {
      renderer.drawText(SMALL_FONT_ID, margin, statusY, line.c_str(), true);
      statusY += smallLineHeight;
    }
  };

  drawWrappedSmall(std::string("BLE: ") + bleState);
  drawWrappedSmall(std::string("Device: ") + X4_TTS_DEVICE_NAME);
  drawWrappedSmall(debugLine1);
  if (!debugLine2.empty()) {
    drawWrappedSmall(debugLine2);
  } else if (!bt.getLastError().empty()) {
    drawWrappedSmall(bt.getLastError());
  }
  drawWrappedSmall(std::string("Commands: ") + std::to_string(commandCount));
  drawWrappedSmall(std::string("Last cmd: ") + lastCommandSummary);

  const int startY = statusY + 6;

  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID) + 2;
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
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);

  state.textDirty = false;
  state.highlightDirty = false;
}

void RemoteTTSReaderActivity::handlePayload(const std::string& payload) {
  LOG_DBG("RTTS", "Incoming payload: %s", payload.c_str());

  JsonDocument doc;
  auto err = deserializeJson(doc, payload);
  if (err) {
    LOG_ERR("RTTS", "JSON parse failed: %s", err.c_str());
    setDebugMessage("JSON parse failed", err.c_str());
    return;
  }

  setDebugMessage("JSON received", payload.substr(0, 40));
  handleCommand(doc);
}

void RemoteTTSReaderActivity::handleCommand(const JsonDocument& doc) {
  if (!doc["type"].is<const char*>()) {
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
    LOG_INF("RTTS", "Cleared document state");
    setDebugMessage("Command: clear");
    return;
  }

  if (type == "load_text") {
    if (!doc["docId"].is<const char*>() || !doc["text"].is<const char*>()) {
      LOG_ERR("RTTS", "Rejected load_text: missing docId/text");
      setDebugMessage("Rejected load_text", "Missing docId/text");
      return;
    }

    state.currentDocId = doc["docId"].as<const char*>();
    state.text = doc["text"].as<const char*>();
    state.highlightStart = 0;
    state.highlightEnd = 0;
    state.textDirty = true;
    state.highlightDirty = true;
    LOG_INF("RTTS", "Loaded docId=%s chars=%d", state.currentDocId.c_str(), state.text.size());
    setDebugMessage("Loaded text", ("docId=" + state.currentDocId).c_str());
    return;
  }

  if (type == "position") {
    if (!doc["docId"].is<const char*>() || !doc["start"].is<int>() || !doc["end"].is<int>()) {
      LOG_ERR("RTTS", "Rejected position: missing fields");
      setDebugMessage("Rejected position", "Missing docId/start/end");
      return;
    }

    const std::string docId = doc["docId"].as<const char*>();
    if (docId != state.currentDocId) {
      LOG_INF("RTTS", "Ignored position for docId=%s current=%s", docId.c_str(), state.currentDocId.c_str());
      setDebugMessage("Ignored position", "docId mismatch");
      return;
    }

    int start = doc["start"].as<int>();
    int end = doc["end"].as<int>();
    if (start > end) {
      std::swap(start, end);
    }
    const int textLen = state.text.size();
    start = std::clamp(start, 0, textLen);
    end = std::clamp(end, 0, textLen);

    state.highlightStart = start;
    state.highlightEnd = end;
    state.highlightDirty = true;
    if (autoFollowHighlight) {
      viewportFirstLine = 0;
    }
    LOG_INF("RTTS", "Updated position %d-%d", start, end);
    setDebugMessage("Position updated", (std::to_string(start) + "-" + std::to_string(end)).c_str());
    return;
  }

  LOG_ERR("RTTS", "Rejected command: unknown type %s", type.c_str());
  setDebugMessage("Rejected command", ("Unknown type: " + type).c_str());
}

void RemoteTTSReaderActivity::wrapText(int maxWidth) {
  wrappedLines.clear();
  if (state.text.empty()) {
    wrappedLines.push_back({"Ready to receive text from phone app.", 0, 0});
    wrappedLines.push_back({"1) Connect to BLE device", 0, 0});
    wrappedLines.push_back({"2) Send load_text command", 0, 0});
    wrappedLines.push_back({"3) Stream position updates", 0, 0});
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
}

void RemoteTTSReaderActivity::clearLoadedContent() {
  state.currentDocId.clear();
  state.text.clear();
  state.highlightStart = 0;
  state.highlightEnd = 0;
  state.textDirty = true;
  state.highlightDirty = true;
}

void RemoteTTSReaderActivity::setDebugMessage(const std::string& line1, const std::string& line2) {
  debugLine1 = line1;
  debugLine2 = line2;
  state.highlightDirty = true;
  requestUpdate();
}
