#include "RemoteTTSReaderActivity.h"
#include <ArduinoJson.h>
#include <HalDisplay.h>
#include "fontIds.h"
#include <BluetoothManager.h>
#include "RemoteTTSConstants.h"

void RemoteTTSReaderActivity::onEnter() {
  Activity::onEnter();
  state.active = true;
  state.text = "Ready for text...";
  viewportFirstLine = 0;
  autoFollowHighlight = true;
  legacyMode.clear();
  BluetoothManager::instance().start(X4_TTS_DEVICE_NAME, [this](const std::string& payload) { handlePayload(payload); });
  state.textDirty = true;
  requestUpdate();
}

void RemoteTTSReaderActivity::onExit() { BluetoothManager::instance().stop(); Activity::onExit(); }

void RemoteTTSReaderActivity::loop() {
  BluetoothManager::instance().poll();
  if (state.textDirty || state.highlightDirty) requestUpdate();
}

void RemoteTTSReaderActivity::render(Activity::RenderLock&&) {
  const int margin = 16;
  const int fontId = BOOKERLY_18_FONT_ID;
  const int bodyLineHeight = renderer.getLineHeight(fontId) + 4;

  if (state.textDirty) wrapText(renderer.getScreenWidth() - (margin * 2));

  // --- Auto-Scroll Logic ---
  if (autoFollowHighlight && !wrappedLines.empty()) {
    int hLine = -1;
    for (size_t i = 0; i < wrappedLines.size(); ++i) {
      if (!(state.highlightEnd <= wrappedLines[i].start || state.highlightStart >= wrappedLines[i].end)) {
        hLine = (int)i; break;
      }
    }
    if (hLine != -1) {
      int maxVis = (renderer.getScreenHeight() - 60) / bodyLineHeight;
      if (hLine < viewportFirstLine || hLine >= (viewportFirstLine + maxVis - 1)) {
        int targetViewport = hLine - (maxVis / 3);
        const int maxFirstLine = std::max(0, (int)wrappedLines.size() - maxVis);
        viewportFirstLine = std::clamp(targetViewport, 0, maxFirstLine);
        state.textDirty = true;
      }
    }
  }

  if (state.textDirty || !renderer.hasBwBufferStored()) {
    renderer.clearScreen();
    renderer.drawText(UI_12_FONT_ID, margin, 4, "X4 Dummy Reader", true);
    int y = 40;
    for (int i = viewportFirstLine; i < (int)wrappedLines.size(); ++i) {
      if (y + bodyLineHeight >= renderer.getScreenHeight() - 20) break;
      renderer.drawText(fontId, margin, y, wrappedLines[i].text.c_str(), true);
      y += bodyLineHeight;
    }
    renderer.storeBwBuffer();
  } else {
    renderer.restoreBwBufferKeep();
  }

  // --- Draw Highlight ---
  int y = 40;
  for (int i = viewportFirstLine; i < (int)wrappedLines.size(); ++i) {
    if (y + bodyLineHeight >= renderer.getScreenHeight() - 20) break;
    if (!(state.highlightEnd <= wrappedLines[i].start || state.highlightStart >= wrappedLines[i].end)) {
      renderer.fillRectDither(margin - 2, y - 1, renderer.getScreenWidth() - (margin * 2) + 4, bodyLineHeight + 1, DarkGray);
      renderer.drawText(fontId, margin, y, wrappedLines[i].text.c_str(), true);
    }
    y += bodyLineHeight;
  }

  renderer.displayHighlightBuffer();
  state.textDirty = false; state.highlightDirty = false;
}

void RemoteTTSReaderActivity::handlePayload(const std::string& payload) {
  if (frameParser.push(payload) == RemoteTTSFrameParser::PushResult::OVERFLOW) return;
  std::string frame;
  while (frameParser.popFrame(frame)) {
    JsonDocument doc;
    if (deserializeJson(doc, frame)) continue;
    const char* type = doc["type"];
    if (!type) continue;

    if (strcmp(type, "load_text") == 0) {
      legacyMode.updateText(doc["text"] | "");
      state.text = legacyMode.text();
      state.textDirty = true;
      viewportFirstLine = 0; // Reset scroll for new window
    } else if (strcmp(type, "position") == 0) {
      legacyMode.setHighlight(doc["start"] | 0, doc["end"] | 0);
      state.highlightStart = legacyMode.highlightStart();
      state.highlightEnd = legacyMode.highlightEnd();
      state.highlightDirty = true;
    }
  }
}

void RemoteTTSReaderActivity::wrapText(int maxWidth) {
  wrappedLines.clear();
  const int fontId = BOOKERLY_18_FONT_ID;
  std::string line; int lineStart = 0, idx = 0, wordStart = 0;
  while (idx <= (int)state.text.size()) {
    const bool atEnd = idx == (int)state.text.size();
    const bool sep = atEnd || state.text[idx] == ' ' || state.text[idx] == '\n';
    if (!sep) { idx++; continue; }
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
  if (!line.empty()) wrappedLines.push_back({line, lineStart, (int)state.text.size()});
}
