#pragma once

#include <cstdint>
#include <set>
#include <vector>

namespace RemoteTTSStreamSequencer {

inline int64_t initialHighestContiguousSeq(uint32_t startSeq) {
  return static_cast<int64_t>(startSeq) - 1;
}

inline uint32_t nextExpectedSeq(int64_t highestContiguousSeq) {
  if (highestContiguousSeq < 0) {
    return 0;
  }
  return static_cast<uint32_t>(highestContiguousSeq + 1);
}

struct ContiguousCommitPlan {
  uint32_t firstCommitTargetSeq = 0;
  int64_t resultingHighestContiguousSeq = -1;
  std::vector<uint32_t> committedSeqs;
  bool encounteredGap = false;
};

inline ContiguousCommitPlan planContiguousCommit(int64_t highestContiguousSeq, uint32_t uptoSeq,
                                                 const std::set<uint32_t>& availableSeqs) {
  ContiguousCommitPlan plan;
  plan.firstCommitTargetSeq = nextExpectedSeq(highestContiguousSeq);
  plan.resultingHighestContiguousSeq = highestContiguousSeq;

  uint32_t seq = plan.firstCommitTargetSeq;
  while (seq <= uptoSeq) {
    if (availableSeqs.find(seq) == availableSeqs.end()) {
      plan.encounteredGap = true;
      break;
    }

    plan.committedSeqs.push_back(seq);
    plan.resultingHighestContiguousSeq = static_cast<int64_t>(seq);
    seq++;
  }

  return plan;
}

}  // namespace RemoteTTSStreamSequencer
