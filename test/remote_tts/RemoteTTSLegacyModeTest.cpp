#include "src/activities/reader/RemoteTTSLegacyMode.h"

#include <cassert>
#include <string>

namespace {

void testChunkedAssemblyFinalize() {
  RemoteTTSLegacyMode legacy(400);
  legacy.clear();
  legacy.onLoadText("doc-a", "Hello ", 0);
  legacy.onLoadText("doc-a", "world", 200);
  assert(legacy.docId().empty());
  assert(legacy.tick(650));
  assert(legacy.docId() == "doc-a");
  assert(legacy.text() == "Hello world");
}

void testDocSwitchMidFlightFinalizesOld() {
  RemoteTTSLegacyMode legacy(500);
  legacy.clear();
  legacy.onLoadText("doc-a", "A", 0);
  legacy.onLoadText("doc-b", "B", 100);
  assert(legacy.docId() == "doc-a");
  assert(legacy.text() == "A");
  assert(legacy.tick(700));
  assert(legacy.docId() == "doc-b");
}

void testPositionBeforeReadyIgnored() {
  RemoteTTSLegacyMode legacy;
  legacy.clear();
  assert(!legacy.onPosition("doc-a", 0, 4));
}

void testUtf8AndClampBehavior() {
  RemoteTTSLegacyMode legacy(100);
  legacy.clear();
  std::string bad = std::string("ok") + static_cast<char>(0xF0);
  legacy.onLoadText("doc-a", bad, 0);
  legacy.tick(200);
  assert(legacy.text() == "ok");

  assert(legacy.onPosition("doc-a", 8, -4));
  assert(legacy.highlightStart() == 0);
  assert(legacy.highlightEnd() == 2);
}

void testRedundantPositionDebounce() {
  RemoteTTSLegacyMode legacy(100);
  legacy.clear();
  legacy.onLoadText("doc-a", "abcd", 0);
  legacy.tick(200);
  assert(legacy.onPosition("doc-a", 1, 1));
  assert(!legacy.onPosition("doc-a", 1, 1));
}

}  // namespace

int main() {
  testChunkedAssemblyFinalize();
  testDocSwitchMidFlightFinalizesOld();
  testPositionBeforeReadyIgnored();
  testUtf8AndClampBehavior();
  testRedundantPositionDebounce();
  return 0;
}
