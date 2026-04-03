#pragma once
#include <string>
#include <algorithm>

class RemoteTTSLegacyMode {
 public:
  void clear() { activeText.clear(); lastStart = 0; lastEnd = 0; }

  void updateText(const std::string& text) {
    activeText = text;
    lastStart = 0;
    lastEnd = 0;
  }

  void setHighlight(int start, int end) {
    lastStart = std::clamp(start, 0, (int)activeText.size());
    lastEnd = std::clamp(end, 0, (int)activeText.size());
  }

  const std::string& text() const { return activeText; }
  int highlightStart() const { return lastStart; }
  int highlightEnd() const { return lastEnd; }

 private:
  std::string activeText;
  int lastStart = 0;
  int lastEnd = 0;
};
