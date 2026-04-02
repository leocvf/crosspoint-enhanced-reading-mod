#pragma once

#include <ArduinoJson.h>

namespace RemoteTTSPacketFieldReaders {

inline bool readUIntAlias(const JsonDocument& doc, const char* primary, const char* alias, uint32_t& outValue) {
  if (doc[primary].is<uint32_t>()) {
    outValue = doc[primary].as<uint32_t>();
    return true;
  }
  if (doc[alias].is<uint32_t>()) {
    outValue = doc[alias].as<uint32_t>();
    return true;
  }
  return false;
}

inline bool readIntAlias(const JsonDocument& doc, const char* primary, const char* alias, int& outValue) {
  if (doc[primary].is<int>()) {
    outValue = doc[primary].as<int>();
    return true;
  }
  if (doc[alias].is<int>()) {
    outValue = doc[alias].as<int>();
    return true;
  }
  return false;
}

}  // namespace RemoteTTSPacketFieldReaders
