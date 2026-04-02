#pragma once

#include <cstdint>
#include <string>

class RemoteTTSFrameParser {
 public:
  explicit RemoteTTSFrameParser(size_t maxBytes) : maxBytes(maxBytes) {}

  enum class PushResult : uint8_t { OK = 0, OVERFLOW };

  PushResult push(const std::string& chunk) {
    if (buffer.size() + chunk.size() > maxBytes) {
      buffer.clear();
      return PushResult::OVERFLOW;
    }
    buffer += chunk;
    return PushResult::OK;
  }

  bool popFrame(std::string& outFrame) {
    size_t scan = 0;
    while (scan < buffer.size() && buffer[scan] != '{') {
      scan++;
    }
    if (scan >= buffer.size()) {
      buffer.clear();
      return false;
    }

    bool inString = false;
    bool escaped = false;
    int depth = 0;
    for (size_t i = scan; i < buffer.size(); ++i) {
      const char ch = buffer[i];
      if (inString) {
        if (escaped) escaped = false;
        else if (ch == '\\') escaped = true;
        else if (ch == '"') inString = false;
        continue;
      }
      if (ch == '"') inString = true;
      else if (ch == '{') depth++;
      else if (ch == '}') {
        depth--;
        if (depth == 0) {
          outFrame = buffer.substr(scan, i - scan + 1);
          buffer.erase(0, i + 1);
          return true;
        }
      }
    }

    if (scan > 0) {
      buffer.erase(0, scan);
    }
    return false;
  }

 private:
  size_t maxBytes;
  std::string buffer;
};
