// Fork branches must obey the same consensus rules as blocks on the tip:
// difficulty required at their height and timestamp bounds, both judged
// against the fork's own history rather than the main chain above the
// common ancestor.

#define private public
#include "chain/ChainManager.hpp"
#undef private

#include "consensus/ConsensusParams.hpp"
#include "consensus/ProofOfWork.hpp"
#include "core/Block.hpp"
#include "crypto/CommonTypes.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

using namespace forgechain::chain;
using namespace forgechain::core;
using forgechain::consensus::ConsensusParams;
using forgechain::consensus::mine_block;
using forgechain::crypto::HashBytes;

namespace {

constexpr size_t kMempoolSize = 1000;
constexpr uint64_t kNow = 1'700'000'000;
constexpr uint64_t kDrift = 120;
constexpr uint32_t kDifficulty = 4;

const ConsensusParams kParams{.initial_difficulty = kDifficulty,
                              .min_difficulty = 1,
                              .target_block_time = 10,
                              .retarget_interval = 20,
                              .mtp_window = 11,
                              .max_future_drift = kDrift};

using Status = BlockOutcome::Status;

struct Fixture {
  uint64_t now{kNow};
  ChainManager manager;

  explicit Fixture(const ConsensusParams &params = kParams)
      : manager(kMempoolSize, params, nullptr, [this] { return now; }) {}

  HashBytes genesis() const { return manager.blockchain_.at(0).hash_; }

  Block extend(size_t count, uint64_t first_timestamp) {
    Block last = manager.blockchain_.latest();
    for (size_t i = 0; i < count; i++) {
      Block block = mine_block(1, manager.latest_hash(), first_timestamp + i,
                               manager.next_block_difficulty(), {});
      EXPECT_EQ(manager.submit_block(block).status, Status::Accepted);
      last = block;
    }
    return last;
  }
};

Block mine_on(const HashBytes &prev, uint64_t timestamp,
              uint32_t difficulty = kDifficulty) {
  return mine_block(1, prev, timestamp, difficulty, {});
}

} // namespace

TEST(ChainManagerForkValidation, SingleOverclaimedBlockCannotReorgDeepHistory) {
  Fixture f;
  Block old_tip = f.extend(5, kNow - 1000);

  Block attack = mine_on(f.genesis(), kNow - 500, 12);
  ASSERT_GT(attack.block_work(), 5u * (1u << kDifficulty));

  auto outcome = f.manager.submit_block(attack);

  EXPECT_NE(outcome.status, Status::Reorged);
  EXPECT_EQ(f.manager.latest_hash(), old_tip.hash_);
  EXPECT_EQ(f.manager.chain_height(), 6u);
}

TEST(ChainManagerForkValidation, UnderclaimedBranchIsRejected) {
  Fixture f;
  Block old_tip = f.extend(1, kNow - 1000);

  Block w1 = mine_on(f.genesis(), kNow - 500, kDifficulty - 1);
  Block w2 = mine_on(w1.hash_, kNow - 499, kDifficulty - 1);
  Block w3 = mine_on(w2.hash_, kNow - 498, kDifficulty - 1);
  ASSERT_GT(3 * w1.block_work(), 1u << kDifficulty)
      << "setup: branch must be heavier than the main chain by raw work";

  f.manager.submit_block(w1);
  f.manager.submit_block(w2);
  auto outcome = f.manager.submit_block(w3);

  EXPECT_NE(outcome.status, Status::Reorged);
  EXPECT_EQ(f.manager.latest_hash(), old_tip.hash_);
}

TEST(ChainManagerForkValidation, OneBadBlockInsideBranchRejectsIt) {
  Fixture f;
  Block old_tip = f.extend(2, kNow - 1000);

  Block w1 = mine_on(f.genesis(), kNow - 500);
  Block w2 = mine_on(w1.hash_, kNow - 499, kDifficulty + 1);
  Block w3 = mine_on(w2.hash_, kNow - 498);

  f.manager.submit_block(w1);
  f.manager.submit_block(w2);
  auto outcome = f.manager.submit_block(w3);

  EXPECT_NE(outcome.status, Status::Reorged);
  EXPECT_EQ(f.manager.latest_hash(), old_tip.hash_);
}

TEST(ChainManagerForkValidation, ValidBranchStillReorgs) {
  Fixture f;
  f.extend(2, kNow - 1000);

  Block w1 = mine_on(f.genesis(), kNow - 500);
  Block w2 = mine_on(w1.hash_, kNow - 499);
  Block w3 = mine_on(w2.hash_, kNow - 498);

  f.manager.submit_block(w1);
  f.manager.submit_block(w2);
  EXPECT_EQ(f.manager.submit_block(w3).status, Status::Reorged);
  EXPECT_EQ(f.manager.latest_hash(), w3.hash_);
}


TEST(ChainManagerForkValidation, FutureTimestampInMiddleOfBranchRejectsIt) {
  Fixture f;
  Block old_tip = f.extend(2, kNow - 1000);

  Block w1 = mine_on(f.genesis(), kNow - 500);
  Block w2 = mine_on(w1.hash_, kNow + kDrift + 1);
  Block w3 = mine_on(w2.hash_, kNow + kDrift + 2);

  f.manager.submit_block(w1);
  f.manager.submit_block(w2);
  auto outcome = f.manager.submit_block(w3);

  EXPECT_NE(outcome.status, Status::Reorged);
  EXPECT_EQ(f.manager.latest_hash(), old_tip.hash_);
}

TEST(ChainManagerForkValidation, FutureBranchAcceptedOnceClockCatchesUp) {
  Fixture f;
  f.extend(2, kNow - 1000);

  Block w1 = mine_on(f.genesis(), kNow - 500);
  Block w2 = mine_on(w1.hash_, kNow + kDrift + 1);
  Block w3 = mine_on(w2.hash_, kNow + kDrift + 2);
  f.manager.submit_block(w1);
  f.manager.submit_block(w2);
  ASSERT_NE(f.manager.submit_block(w3).status, Status::Reorged);

  f.now += 10;
  EXPECT_EQ(f.manager.submit_block(w3).status, Status::Reorged);
  EXPECT_EQ(f.manager.latest_hash(), w3.hash_);
}

TEST(ChainManagerForkValidation, ForkTimestampsJudgedAgainstForkHistory) {
  Fixture f;
  f.extend(2, kNow - 100);

  Block w1 = mine_on(f.genesis(), kNow - 900);
  Block w2 = mine_on(w1.hash_, kNow - 899);
  Block w3 = mine_on(w2.hash_, kNow - 898);

  f.manager.submit_block(w1);
  f.manager.submit_block(w2);
  EXPECT_EQ(f.manager.submit_block(w3).status, Status::Reorged);
  EXPECT_EQ(f.manager.latest_hash(), w3.hash_);
}

TEST(ChainManagerForkValidation, MtpWindowSpansCommonAncestor) {
  Fixture f;
  f.extend(1, kNow - 1000);
  f.extend(1, kNow - 990);
  Block a3 = f.extend(1, kNow - 980);
  Block old_tip = f.extend(1, kNow - 970);

  Block below_mtp = mine_on(a3.hash_, kNow - 995);
  Block child = mine_on(below_mtp.hash_, kNow - 960);
  f.manager.submit_block(below_mtp);
  EXPECT_NE(f.manager.submit_block(child).status, Status::Reorged);
  EXPECT_EQ(f.manager.latest_hash(), old_tip.hash_);

  Block above_mtp = mine_on(a3.hash_, kNow - 989);
  Block child2 = mine_on(above_mtp.hash_, kNow - 959);
  f.manager.submit_block(above_mtp);
  EXPECT_EQ(f.manager.submit_block(child2).status, Status::Reorged);
  EXPECT_EQ(f.manager.latest_hash(), child2.hash_);
}


TEST(ChainManagerForkValidation, HeaviestInvalidBranchFallsBackToValidOne) {
  Fixture f;
  f.extend(2, kNow - 1000);

  Block p = mine_on(f.genesis(), kNow - 500);
  Block bad1 = mine_on(p.hash_, kNow - 499, 8);
  Block good1 = mine_on(p.hash_, kNow - 498);
  Block good2 = mine_on(good1.hash_, kNow - 497);

  f.manager.submit_block(bad1);
  f.manager.submit_block(good1);
  f.manager.submit_block(good2);
  auto outcome = f.manager.submit_block(p);

  EXPECT_EQ(outcome.status, Status::Reorged);
  EXPECT_EQ(f.manager.latest_hash(), good2.hash_);
  EXPECT_FALSE(f.manager.blockchain_.has_block(bad1.hash_));
}

TEST(ChainManagerForkValidation, RetargetInsideForkUsesForkTimestamps) {
  const ConsensusParams params{.initial_difficulty = 4,
                               .min_difficulty = 1,
                               .target_block_time = 10,
                               .retarget_interval = 2,
                               .mtp_window = 11,
                               .max_future_drift = kDrift};
  Fixture f(params);
  Block c1 = f.extend(1, kNow - 1000);
  f.extend(1, kNow - 990);
  f.extend(1, kNow - 980);
  ASSERT_EQ(f.manager.next_block_difficulty(), 4u);
  f.extend(1, kNow - 970);
  Block old_tip = f.manager.blockchain_.latest();

  Block f2 = mine_on(c1.hash_, kNow - 900, 4);
  Block f3 = mine_on(f2.hash_, kNow - 899, 4);

  Block wrong4 = mine_on(f3.hash_, kNow - 898, 4);
  Block wrong5 = mine_on(wrong4.hash_, kNow - 897, 4);
  f.manager.submit_block(f2);
  f.manager.submit_block(f3);
  f.manager.submit_block(wrong4);
  EXPECT_NE(f.manager.submit_block(wrong5).status, Status::Reorged);
  EXPECT_EQ(f.manager.latest_hash(), old_tip.hash_);

  Block right4 = mine_on(f3.hash_, kNow - 896, 6);
  EXPECT_EQ(f.manager.submit_block(right4).status, Status::Reorged);
  EXPECT_EQ(f.manager.latest_hash(), right4.hash_);
}
