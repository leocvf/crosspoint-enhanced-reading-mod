#include "src/activities/reader/RemoteTTSStreamSequencer.h"

#include <cassert>
#include <cstdint>
#include <set>

namespace {

void testStartSeqZeroCommitsSeqZeroFirst() {
  const int64_t highest = RemoteTTSStreamSequencer::initialHighestContiguousSeq(0);
  assert(highest == -1);
  assert(RemoteTTSStreamSequencer::nextExpectedSeq(highest) == 0);

  const std::set<uint32_t> available = {0};
  const auto plan = RemoteTTSStreamSequencer::planContiguousCommit(highest, 0, available);
  assert(plan.firstCommitTargetSeq == 0);
  assert(plan.committedSeqs.size() == 1);
  assert(plan.committedSeqs[0] == 0);
  assert(plan.resultingHighestContiguousSeq == 0);
  assert(!plan.encounteredGap);
}

void testStartSeqOneCommitsSeqOneFirst() {
  const int64_t highest = RemoteTTSStreamSequencer::initialHighestContiguousSeq(1);
  assert(highest == 0);
  assert(RemoteTTSStreamSequencer::nextExpectedSeq(highest) == 1);

  const std::set<uint32_t> available = {1};
  const auto plan = RemoteTTSStreamSequencer::planContiguousCommit(highest, 1, available);
  assert(plan.firstCommitTargetSeq == 1);
  assert(plan.committedSeqs.size() == 1);
  assert(plan.committedSeqs[0] == 1);
  assert(plan.resultingHighestContiguousSeq == 1);
  assert(!plan.encounteredGap);
}

void testOutOfOrderCatchUpCommit() {
  const int64_t initialHighest = RemoteTTSStreamSequencer::initialHighestContiguousSeq(0);
  const std::set<uint32_t> missingZero = {1};
  const auto firstPlan = RemoteTTSStreamSequencer::planContiguousCommit(initialHighest, 1, missingZero);
  assert(firstPlan.firstCommitTargetSeq == 0);
  assert(firstPlan.committedSeqs.empty());
  assert(firstPlan.encounteredGap);
  assert(firstPlan.resultingHighestContiguousSeq == -1);

  const std::set<uint32_t> caughtUp = {0, 1};
  const auto secondPlan = RemoteTTSStreamSequencer::planContiguousCommit(firstPlan.resultingHighestContiguousSeq, 1, caughtUp);
  assert(secondPlan.firstCommitTargetSeq == 0);
  assert(secondPlan.committedSeqs.size() == 2);
  assert(secondPlan.committedSeqs[0] == 0);
  assert(secondPlan.committedSeqs[1] == 1);
  assert(secondPlan.resultingHighestContiguousSeq == 1);
  assert(!secondPlan.encounteredGap);
}

}  // namespace

int main() {
  testStartSeqZeroCommitsSeqZeroFirst();
  testStartSeqOneCommitsSeqOneFirst();
  testOutOfOrderCatchUpCommit();
  return 0;
}
