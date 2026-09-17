// adopt_branch is the catch-up path: a branch the node asked for, arriving
// whole. Unlike ordinary fork handling it is not capped at kMaxForkDepth --
// that cap exists to make unsolicited blocks cheap to reject, and a branch we
// requested is not unsolicited.

#define private public
#include "chain/ChainManager.hpp"
#undef private

#include "consensus/ConsensusParams.hpp"
#include "consensus/ProofOfWork.hpp"
#include "core/Block.hpp"
#include "core/ForkResolution.hpp"
#include "crypto/CommonTypes.hpp"

#include <gtest/gtest.h>
#include <utility>
#include <cstddef>
#include <cstdint>
#include <vector>

using namespace forgechain::chain;
using namespace forgechain::core;
using forgechain::consensus::ConsensusParams;
using forgechain::consensus::mine_block;
using forgechain::crypto::HashBytes;

namespace {

constexpr size_t kMempoolSize = 1000;
constexpr uint64_t kNow = 1'700'000'000;
constexpr uint32_t kDifficulty = 2;

const ConsensusParams kParams{.initial_difficulty = kDifficulty,
                              .min_difficulty = 1,
                              .target_block_time = 10,
                              .retarget_interval = 100000,
                              .mtp_window = 11,
                              .max_future_drift = 120};

using Status = BlockOutcome::Status;

struct Fixture {
  uint64_t now{kNow + 100000};
  ChainManager manager;

  Fixture() : manager(kMempoolSize, kParams, nullptr, [this] { return now; }) {}

  void extend(size_t count, uint64_t first_timestamp) {
    for (size_t i = 0; i < count; i++) {
      Block block = mine_block(1, manager.latest_hash(), first_timestamp + i,
                               manager.next_block_difficulty(), {});
      ASSERT_EQ(manager.submit_block(block).status, Status::Accepted);
    }
  }
};

// A standalone branch hanging off `prev`, as it would arrive over the wire.
std::vector<Block> make_branch(const HashBytes &prev, size_t count,
                               uint64_t first_timestamp,
                               uint32_t difficulty = kDifficulty) {
  std::vector<Block> branch;
  HashBytes parent = prev;
  for (size_t i = 0; i < count; i++) {
    Block block = mine_block(1, parent, first_timestamp + i, difficulty, {});
    parent = block.hash_;
    branch.push_back(block);
  }
  return branch;
}

} // namespace

TEST(AdoptBranch, AdoptsABranchFarDeeperThanTheForkDepthCap) {
  Fixture f;
  f.extend(20, kNow);
  const HashBytes fork_point = f.manager.latest_hash();
  f.extend(100, kNow + 20);
  ASSERT_EQ(f.manager.chain_height(), 121u);

  auto branch = make_branch(fork_point, 150, kNow + 5000);
  const HashBytes new_tip = branch.back().hash_;

  EXPECT_TRUE(f.manager.adopt_branch(std::move(branch)));
  EXPECT_EQ(f.manager.latest_hash(), new_tip);
  EXPECT_EQ(f.manager.chain_height(), 171u);
}

TEST(AdoptBranch, AdoptsABranchThatSimplyExtendsTheTip) {
  Fixture f;
  f.extend(5, kNow);
  const HashBytes tip = f.manager.latest_hash();

  auto branch = make_branch(tip, 40, kNow + 1000);
  const HashBytes new_tip = branch.back().hash_;

  EXPECT_TRUE(f.manager.adopt_branch(std::move(branch)));
  EXPECT_EQ(f.manager.latest_hash(), new_tip);
  EXPECT_EQ(f.manager.chain_height(), 46u);
}

TEST(AdoptBranch, SkipsBlocksTheNodeAlreadyHas) {
  Fixture f;
  f.extend(30, kNow);
  std::vector<Block> branch;
  for (size_t h = 11; h < f.manager.chain_height(); h++) {
    branch.push_back(f.manager.block_at(h));
  }
  size_t overlap = branch.size();
  auto extra = make_branch(f.manager.latest_hash(), 10, kNow + 2000);
  branch.insert(branch.end(), extra.begin(), extra.end());
  const HashBytes new_tip = branch.back().hash_;

  EXPECT_TRUE(f.manager.adopt_branch(std::move(branch)));
  EXPECT_EQ(f.manager.latest_hash(), new_tip);
  EXPECT_EQ(f.manager.chain_height(), 31u + 10u);
  EXPECT_GT(overlap, 0u) << "test setup: there should be an overlap";
}

TEST(AdoptBranch, RejectsABranchAlreadyFullyKnown) {
  Fixture f;
  f.extend(10, kNow);
  std::vector<Block> branch;
  for (size_t h = 1; h < f.manager.chain_height(); h++) {
    branch.push_back(f.manager.block_at(h));
  }
  const HashBytes tip = f.manager.latest_hash();

  EXPECT_FALSE(f.manager.adopt_branch(std::move(branch)));
  EXPECT_EQ(f.manager.latest_hash(), tip);
}

TEST(AdoptBranch, RejectsAnEmptyBranch) {
  Fixture f;
  f.extend(3, kNow);
  const HashBytes tip = f.manager.latest_hash();

  EXPECT_FALSE(f.manager.adopt_branch({}));
  EXPECT_EQ(f.manager.latest_hash(), tip);
}

TEST(AdoptBranch, RejectsABranchWithNoAnchorInOurChain) {
  Fixture f;
  f.extend(10, kNow);
  const HashBytes tip = f.manager.latest_hash();

  HashBytes stranger{};
  stranger.fill(0x7f);
  auto branch = make_branch(stranger, 20, kNow + 3000);

  EXPECT_FALSE(f.manager.adopt_branch(std::move(branch)));
  EXPECT_EQ(f.manager.latest_hash(), tip);
}

TEST(AdoptBranch, RejectsABranchWithAGap) {
  Fixture f;
  f.extend(10, kNow);
  const HashBytes tip = f.manager.latest_hash();
  auto branch = make_branch(tip, 20, kNow + 3000);
  branch.erase(branch.begin() + 10);

  EXPECT_FALSE(f.manager.adopt_branch(std::move(branch)));
  EXPECT_EQ(f.manager.latest_hash(), tip);
}

TEST(AdoptBranch, RejectsABranchOutOfOrder) {
  Fixture f;
  f.extend(10, kNow);
  const HashBytes tip = f.manager.latest_hash();
  auto branch = make_branch(tip, 20, kNow + 3000);
  std::swap(branch[5], branch[6]);

  EXPECT_FALSE(f.manager.adopt_branch(std::move(branch)));
  EXPECT_EQ(f.manager.latest_hash(), tip);
}

TEST(AdoptBranch, RejectsABranchLighterThanOurChain) {
  Fixture f;
  f.extend(10, kNow);
  const HashBytes fork_point = f.manager.block_at(5).hash_;
  const HashBytes tip = f.manager.latest_hash();

  auto branch = make_branch(fork_point, 3, kNow + 3000);

  EXPECT_FALSE(f.manager.adopt_branch(std::move(branch)));
  EXPECT_EQ(f.manager.latest_hash(), tip);
  EXPECT_EQ(f.manager.chain_height(), 11u);
}

TEST(AdoptBranch, TruncatesAtTheFirstInvalidBlock) {
  Fixture f;
  f.extend(5, kNow);
  const HashBytes tip = f.manager.latest_hash();

  auto branch = make_branch(tip, 10, kNow + 3000);
  auto bad = make_branch(branch.back().hash_, 1, kNow + 3020, kDifficulty + 2);
  auto rest = make_branch(bad.back().hash_, 10, kNow + 3030);
  const HashBytes expected_tip = branch.back().hash_;
  branch.insert(branch.end(), bad.begin(), bad.end());
  branch.insert(branch.end(), rest.begin(), rest.end());

  EXPECT_TRUE(f.manager.adopt_branch(std::move(branch)));
  EXPECT_EQ(f.manager.latest_hash(), expected_tip);
  EXPECT_EQ(f.manager.chain_height(), 16u);
}

TEST(AdoptBranch, RejectsABranchWhoseFirstNewBlockIsInvalid) {
  Fixture f;
  f.extend(5, kNow);
  const HashBytes tip = f.manager.latest_hash();
  auto branch = make_branch(tip, 20, kNow + 3000, kDifficulty + 2);

  EXPECT_FALSE(f.manager.adopt_branch(std::move(branch)));
  EXPECT_EQ(f.manager.latest_hash(), tip);
}

TEST(AdoptBranch, RejectsBlocksFromTheFuture) {
  Fixture f;
  f.extend(5, kNow);
  const HashBytes tip = f.manager.latest_hash();
  auto branch = make_branch(tip, 20, f.now + 1000);

  EXPECT_FALSE(f.manager.adopt_branch(std::move(branch)));
  EXPECT_EQ(f.manager.latest_hash(), tip);
}
