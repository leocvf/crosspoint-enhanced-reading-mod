#include "RemoteTTSReaderActivity.h"

#include <HalDisplay.h>
#include <Logging.h>

#include <algorithm>

#include "fontIds.h"
#include <BluetoothManager.h>

void RemoteTTSReaderActivity::onEnter() {
  Activity::onEnter();
  LOG_INF("RTTS", "Entering Remote TTS Reader mode");
  state.active = true;
  setDemoContent();

  const bool started = BluetoothManager::instance().start(
      "CrossPoint-X4-TTS", [this](const std::string& payload) { handlePayload(payload); });
  if (!started) {
    LOG_ERR("RTTS", "BluetoothManager failed to start");
    setDebugMessage("BLE start failed", "Check serial logs");
  } else {
    setDebugMessage("BLE advertising", "Use Android BLE client to write JSON");
  }
  lastConnectedState = BluetoothManager::instance().isConnected();
  requestUpdate();
}

void RemoteTTSReaderActivity::onExit() {
  LOG_INF("RTTS", "Exiting Remote TTS Reader mode");
  BluetoothManager::instance().stop();
  Activity::onExit();
}

void RemoteTTSReaderActivity::loop() {
  BluetoothManager::instance().poll();
  const bool connected = BluetoothManager::instance().isConnected();
  if (connected != lastConnectedState) {
    lastConnectedState = connected;
    if (connected) {
      setDebugMessage("BLE connected", "Waiting for load_text");
    } else {
      setDebugMessage("BLE disconnected", "Advertising again");
    }
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    onExitToHome();
    return;
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
  const int startY = 40;

  if (state.textDirty) {
    wrapText(contentWidth);
  }

  renderer.clearScreen();
  renderer.drawText(UI_12_FONT_ID, margin, 6, "Remote TTS Reader", true, EpdFontFamily::BOLD);
  const BluetoothManager& bt = BluetoothManager::instance();
  const char* bleState = bt.isConnected() ? "Connected" : (bt.isStarted() ? "Advertising" : "Stopped");
  renderer.drawText(SMALL_FONT_ID, margin, 20, bleState, true);
  if (!debugLine1.empty()) {
    renderer.drawText(SMALL_FONT_ID, margin + 88, 20, debugLine1.c_str(), true);
  }
  if (!debugLine2.empty()) {
    renderer.drawText(SMALL_FONT_ID, margin, 30, debugLine2.c_str(), true);
  }

  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID) + 2;
  int y = startY;
  for (const auto& line : wrappedLines) {
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

  renderer.drawText(SMALL_FONT_ID, margin, screenHeight - 10, "Back: Home");
  renderer.displayBuffer((state.textDirty) ? HalDisplay::FULL_REFRESH : HalDisplay::HALF_REFRESH);

  state.textDirty = false;
  state.highlightDirty = false;
}

void RemoteTTSReaderActivity::handlePayload(const std::string& payload) {
  LOG_DBG("RTTS", "Incoming payload: %s", payload.c_str());

  StaticJsonDocument<2048> doc;
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

  if (type == "ping") {
    LOG_INF("RTTS", "Received ping");
    setDebugMessage("Command: ping");
    return;
  }

  if (type == "clear") {
    state.currentDocId.clear();
    state.text.clear();
    state.highlightStart = 0;
    state.highlightEnd = 0;
    state.textDirty = true;
    state.highlightDirty = true;
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
    wrappedLines.push_back({"Waiting for load_text over BLE...", 0, 0});
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
  state.currentDocId = "demo-doc";
  state.text =
      "Remote TTS Reader mode is active. This is demo text while BLE starts. Send load_text and position packets "
      "from Android to replace this content.";
  state.highlightStart = 36;
  state.highlightEnd = 80;
  state.textDirty = true;
  state.highlightDirty = true;
}

void RemoteTTSReaderActivity::setDebugMessage(const std::string& line1, const std::string& line2) {
  debugLine1 = line1;
  debugLine2 = line2;
  state.highlightDirty = true;
}
