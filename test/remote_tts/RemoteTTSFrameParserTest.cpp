#include "src/activities/reader/RemoteTTSFrameParser.h"

#include <cassert>
#include <string>

int main() {
  RemoteTTSFrameParser parser(128);
  std::string frame;

  assert(parser.push("{\"type\":\"ping\"") == RemoteTTSFrameParser::PushResult::OK);
  assert(!parser.popFrame(frame));
  assert(parser.push("}\n{bad json}\n{\"type\":\"clear\"}") == RemoteTTSFrameParser::PushResult::OK);
  assert(parser.popFrame(frame));
  assert(frame == "{\"type\":\"ping\"}");
  assert(parser.popFrame(frame));
  assert(frame == "{bad json}");
  assert(parser.popFrame(frame));
  assert(frame == "{\"type\":\"clear\"}");
  assert(!parser.popFrame(frame));
  return 0;
}
