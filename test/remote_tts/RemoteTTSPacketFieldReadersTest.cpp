#include "src/activities/reader/RemoteTTSPacketFieldReaders.h"

#include <ArduinoJson.h>

#include <cassert>
#include <cstdint>

namespace {

JsonDocument parseDoc(const char* json) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  assert(!err);
  return doc;
}

void testReadUIntAliasCanonicalAndAlias() {
  uint32_t value = 0;

  JsonDocument canonical = parseDoc(R"({"seq":7})");
  assert(RemoteTTSPacketFieldReaders::readUIntAlias(canonical, "seq", "sequenceId", value));
  assert(value == 7);

  JsonDocument alias = parseDoc(R"({"sequenceId":9})");
  assert(RemoteTTSPacketFieldReaders::readUIntAlias(alias, "seq", "sequenceId", value));
  assert(value == 9);
}

void testReadUIntAliasPrefersCanonicalWhenBothPresent() {
  uint32_t value = 0;
  JsonDocument both = parseDoc(R"({"uptoSeq":15,"committedSeq":22})");
  assert(RemoteTTSPacketFieldReaders::readUIntAlias(both, "uptoSeq", "committedSeq", value));
  assert(value == 15);
}

void testReadIntAliasCanonicalAndAlias() {
  int value = -1;

  JsonDocument canonical = parseDoc(R"({"offset":120})");
  assert(RemoteTTSPacketFieldReaders::readIntAlias(canonical, "offset", "start", value));
  assert(value == 120);

  JsonDocument alias = parseDoc(R"({"start":42})");
  assert(RemoteTTSPacketFieldReaders::readIntAlias(alias, "offset", "start", value));
  assert(value == 42);
}

void testReadIntAliasPrefersCanonicalWhenBothPresent() {
  int value = -1;
  JsonDocument both = parseDoc(R"({"offset":20,"start":99})");
  assert(RemoteTTSPacketFieldReaders::readIntAlias(both, "offset", "start", value));
  assert(value == 20);
}

void testMalformedAndMissingPacketsAreRejected() {
  uint32_t seq = 0;
  int offset = 0;

  JsonDocument missingBoth = parseDoc(R"({"text":"hello"})");
  assert(!RemoteTTSPacketFieldReaders::readUIntAlias(missingBoth, "seq", "sequenceId", seq));
  assert(!RemoteTTSPacketFieldReaders::readIntAlias(missingBoth, "offset", "start", offset));

  JsonDocument malformedTypes = parseDoc(R"({"seq":"1","offset":"7","sequenceId":"2","start":"3"})");
  assert(!RemoteTTSPacketFieldReaders::readUIntAlias(malformedTypes, "seq", "sequenceId", seq));
  assert(!RemoteTTSPacketFieldReaders::readIntAlias(malformedTypes, "offset", "start", offset));
}

}  // namespace

int main() {
  testReadUIntAliasCanonicalAndAlias();
  testReadUIntAliasPrefersCanonicalWhenBothPresent();
  testReadIntAliasCanonicalAndAlias();
  testReadIntAliasPrefersCanonicalWhenBothPresent();
  testMalformedAndMissingPacketsAreRejected();
  return 0;
}
