// Fork blocks can arrive in any order over the network. These tests pin down
// that ChainManager reaches the same chain regardless of arrival order, and
// that blocks which made it into the chain are dropped from the orphan pool.

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
#include <vector>

using namespace forgechain::chain;
using namespace forgechain::core;
using forgechain::consensus::kTestParams;
using forgechain::consensus::mine_block;
using forgechain::crypto::HashBytes;

namespace {

constexpr size_t kMempoolSize = 1000;
constexpr uint32_t kDifficulty = kTestParams.initial_difficulty;

Block mine_on(const HashBytes &prev, uint64_t timestamp) {
  return mine_block(1, prev, timestamp, kDifficulty, {});
}

using Status = BlockOutcome::Status;

} // namespace

TEST(ChainManagerForkOrder, ForwardOrderStillReorgs) {
  ChainManager manager(kMempoolSize, kTestParams);
  const HashBytes genesis = manager.latest_hash();
  Block losing = mine_on(genesis, 1000);
  ASSERT_EQ(manager.submit_block(losing).status, Status::Accepted);

  Block win1 = mine_on(genesis, 2000);
  Block win2 = mine_on(win1.hash_, 2001);

  EXPECT_EQ(manager.submit_block(win1).status, Status::Rejected)
      << "equal work must not reorg";
  auto outcome = manager.submit_block(win2);

  EXPECT_EQ(outcome.status, Status::Reorged);
  EXPECT_EQ(manager.latest_hash(), win2.hash_);
}

TEST(ChainManagerForkOrder, ChildBeforeParentReorgs) {
  ChainManager manager(kMempoolSize, kTestParams);
  const HashBytes genesis = manager.latest_hash();
  Block losing = mine_on(genesis, 1000);
  ASSERT_EQ(manager.submit_block(losing).status, Status::Accepted);

  Block win1 = mine_on(genesis, 2000);
  Block win2 = mine_on(win1.hash_, 2001);

  auto first = manager.submit_block(win2);
  EXPECT_EQ(first.status, Status::NeedParent);
  ASSERT_TRUE(first.missing_parent.has_value());
  EXPECT_EQ(*first.missing_parent, win1.hash_);

  auto second = manager.submit_block(win1);

  EXPECT_EQ(second.status, Status::Reorged);
  EXPECT_EQ(manager.latest_hash(), win2.hash_);
  EXPECT_EQ(manager.chain_height(), 3u);
  EXPECT_EQ(second.to_broadcast, (std::vector<HashBytes>{win1.hash_, win2.hash_}));
}

TEST(ChainManagerForkOrder, ThreeBlocksInReverseOrder) {
  ChainManager manager(kMempoolSize, kTestParams);
  const HashBytes genesis = manager.latest_hash();
  Block losing1 = mine_on(genesis, 1000);
  ASSERT_EQ(manager.submit_block(losing1).status, Status::Accepted);
  Block losing2 = mine_on(losing1.hash_, 1001);
  ASSERT_EQ(manager.submit_block(losing2).status, Status::Accepted);

  Block w1 = mine_on(genesis, 2000);
  Block w2 = mine_on(w1.hash_, 2001);
  Block w3 = mine_on(w2.hash_, 2002);

  EXPECT_EQ(manager.submit_block(w3).status, Status::NeedParent);
  EXPECT_EQ(manager.submit_block(w2).status, Status::NeedParent);
  EXPECT_EQ(manager.latest_hash(), losing2.hash_);

  auto outcome = manager.submit_block(w1);

  EXPECT_EQ(outcome.status, Status::Reorged);
  EXPECT_EQ(manager.latest_hash(), w3.hash_);
  EXPECT_EQ(manager.chain_height(), 4u);
}

TEST(ChainManagerForkOrder, MiddleBlockArrivesLast) {
  ChainManager manager(kMempoolSize, kTestParams);
  const HashBytes genesis = manager.latest_hash();
  Block losing1 = mine_on(genesis, 1000);
  ASSERT_EQ(manager.submit_block(losing1).status, Status::Accepted);
  Block losing2 = mine_on(losing1.hash_, 1001);
  ASSERT_EQ(manager.submit_block(losing2).status, Status::Accepted);

  Block w1 = mine_on(genesis, 2000);
  Block w2 = mine_on(w1.hash_, 2001);
  Block w3 = mine_on(w2.hash_, 2002);

  EXPECT_EQ(manager.submit_block(w1).status, Status::Rejected);
  EXPECT_EQ(manager.submit_block(w3).status, Status::NeedParent);

  auto outcome = manager.submit_block(w2);

  EXPECT_EQ(outcome.status, Status::Reorged);
  EXPECT_EQ(manager.latest_hash(), w3.hash_);
}

TEST(ChainManagerForkOrder, TwoChildrenPicksHeavierBranchInOneReorg) {
  ChainManager manager(kMempoolSize, kTestParams);
  const HashBytes genesis = manager.latest_hash();
  Block losing1 = mine_on(genesis, 1000);
  ASSERT_EQ(manager.submit_block(losing1).status, Status::Accepted);
  Block losing2 = mine_on(losing1.hash_, 1001);
  ASSERT_EQ(manager.submit_block(losing2).status, Status::Accepted);

  Block p = mine_on(genesis, 2000);
  Block short1 = mine_on(p.hash_, 2001);
  Block long1 = mine_on(p.hash_, 2002);
  Block long2 = mine_on(long1.hash_, 2003);

  EXPECT_EQ(manager.submit_block(short1).status, Status::NeedParent);
  EXPECT_EQ(manager.submit_block(long1).status, Status::NeedParent);
  EXPECT_EQ(manager.submit_block(long2).status, Status::NeedParent);

  auto outcome = manager.submit_block(p);

  EXPECT_EQ(outcome.status, Status::Reorged);
  EXPECT_EQ(manager.latest_hash(), long2.hash_);
  EXPECT_FALSE(manager.blockchain_.has_block(short1.hash_));
  EXPECT_EQ(outcome.to_broadcast,
            (std::vector<HashBytes>{p.hash_, long1.hash_, long2.hash_}));
}

TEST(ChainManagerForkOrder, ReorgRemovesNewBranchFromPool) {
  ChainManager manager(kMempoolSize, kTestParams);
  const HashBytes genesis = manager.latest_hash();
  Block losing = mine_on(genesis, 1000);
  ASSERT_EQ(manager.submit_block(losing).status, Status::Accepted);

  Block win1 = mine_on(genesis, 2000);
  Block win2 = mine_on(win1.hash_, 2001);
  manager.submit_block(win2);
  ASSERT_EQ(manager.submit_block(win1).status, Status::Reorged);

  EXPECT_FALSE(manager.orphan_pool_.has_orphan(win1.hash_));
  EXPECT_FALSE(manager.orphan_pool_.has_orphan(win2.hash_));
  EXPECT_EQ(manager.orphan_pool_.orphan_count(), 0u);
}

TEST(ChainManagerForkOrder, LosingSiblingStaysInPool) {
  ChainManager manager(kMempoolSize, kTestParams);
  const HashBytes genesis = manager.latest_hash();
  Block losing1 = mine_on(genesis, 1000);
  ASSERT_EQ(manager.submit_block(losing1).status, Status::Accepted);
  Block losing2 = mine_on(losing1.hash_, 1001);
  ASSERT_EQ(manager.submit_block(losing2).status, Status::Accepted);

  Block p = mine_on(genesis, 2000);
  Block short1 = mine_on(p.hash_, 2001);
  Block long1 = mine_on(p.hash_, 2002);
  Block long2 = mine_on(long1.hash_, 2003);
  manager.submit_block(short1);
  manager.submit_block(long1);
  manager.submit_block(long2);
  ASSERT_EQ(manager.submit_block(p).status, Status::Reorged);

  EXPECT_TRUE(manager.orphan_pool_.has_orphan(short1.hash_))
      << "only blocks that entered the chain may be removed";
  EXPECT_EQ(manager.orphan_pool_.orphan_count(), 1u);
}

TEST(ChainManagerForkOrder, DescendantsWithoutParentStillNeedParent) {
  ChainManager manager(kMempoolSize, kTestParams);
  const HashBytes genesis = manager.latest_hash();

  Block w1 = mine_on(genesis, 2000);
  Block w2 = mine_on(w1.hash_, 2001);
  Block w3 = mine_on(w2.hash_, 2002);

  EXPECT_EQ(manager.submit_block(w3).status, Status::NeedParent);
  EXPECT_EQ(manager.submit_block(w2).status, Status::NeedParent);

  EXPECT_EQ(manager.chain_height(), 1u);
  EXPECT_EQ(manager.orphan_pool_.orphan_count(), 2u);
}
