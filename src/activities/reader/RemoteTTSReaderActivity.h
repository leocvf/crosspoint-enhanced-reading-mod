#pragma once

#include <ArduinoJson.h>

#include <functional>
#include <string>
#include <vector>

#include "activities/Activity.h"

class RemoteTTSReaderActivity : public Activity {
 public:
  explicit RemoteTTSReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                   const std::function<void()>& onExitToHome)
      : Activity("RemoteTTS", renderer, mappedInput), onExitToHome(onExitToHome) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(Activity::RenderLock&&) override;

 private:
  struct State {
    bool active = false;
    std::string currentDocId;
    std::string text;
    int highlightStart = 0;
    int highlightEnd = 0;
    bool textDirty = true;
    bool highlightDirty = true;
  } state;

  struct WrappedLine {
    std::string text;
    int start = 0;
    int end = 0;
  };

  std::vector<WrappedLine> wrappedLines;
  const std::function<void()> onExitToHome;
  std::string debugLine1;
  std::string debugLine2;
  bool lastConnectedState = false;

  void handlePayload(const std::string& payload);
  void handleCommand(const JsonDocument& doc);
  void wrapText(int maxWidth);
  void setDemoContent();
  void setDebugMessage(const std::string& line1, const std::string& line2 = "");
};
