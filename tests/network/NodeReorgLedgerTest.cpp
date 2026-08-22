// White-box test for Node::try_reorg.
//
// try_reorg is private, and it is the one place where a fork switch has to
// keep three independent pieces of state consistent with each other:
// Blockchain (which blocks are canonical), Mempool (which pending txs exist),
// and Ledger (account balances). This file uses #define private public to
// reach try_reorg directly, build competing chains in-process (no sockets,
// no mining), and assert on all three side effects together -- plus the
// return value used for INV rebroadcast.
//
// Ground truth for Ledger correctness: an independent Ledger that replays
// only the winning branch from genesis. If Node's ledger_ matches that after
// try_reorg, the incremental apply/reverse bookkeeping is provably correct
// for that scenario, independent of how it got there.

// All standard-library / third-party headers that Node.hpp transitively
// pulls in must be included BEFORE the private->public macro swap below.
// Otherwise the macro is still active while libstdc++ internal headers
// (e.g. <chrono>'s transitive include of <sstream>) are parsed, which
// breaks unrelated private/public declarations inside those headers.
#include "consensus/ProofOfWork.hpp"
#include "core/Block.hpp"
#include "core/Blockchain.hpp"
#include "core/Ledger.hpp"
#include "core/Mempool.hpp"
#include "core/OrphanPool.hpp"
#include "core/Transaction.hpp"
#include "crypto/Address.hpp"
#include "crypto/CommonTypes.hpp"
#include "crypto/Keys.hpp"
#include "crypto/Signature.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>
#include "network/Handshake.hpp"
#define private public
#include "network/Node.hpp"
#undef private

using namespace forgechain::network;
using namespace forgechain::core;
using namespace forgechain::crypto;

namespace {


struct Wallet {
  KeyPair keys;
  str address;
};

Wallet make_wallet() {
  KeyPair kp = generate_keypair();
  return Wallet{kp, derive_address(kp.public_key)};
}

Transaction make_signed_tx(const Wallet &sender, const str &recipient,
                            uint64_t amount) {
  Transaction tx(sender.address, recipient, amount, sender.keys.public_key);
  tx.signature_ = sign(tx.serialize_for_signing(), sender.keys.private_key);
  return tx;
}


Block make_block(const HashBytes &prev_hash, uint64_t timestamp_seed,
                  std::vector<Transaction> txs, uint32_t difficulty = 0) {
  Block b(1, prev_hash, timestamp_seed, std::move(txs));
  b.difficulty_ = difficulty;
  return b;
}

struct TestNode : Node {
  Blockchain chain;
  Mempool mempool;
  OrphanPool orphan_pool;
  Ledger ledger;

  TestNode()
      : Node(0, VersionInfo{.protocol_version = 1, .chain_height = 0,
                            .timestamp = 0},
             chain, mempool, orphan_pool, ledger) {}
};


Ledger recompute_ledger_from_genesis(
    const std::vector<std::pair<str, uint64_t>> &initial_balances,
    const std::vector<Block> &chain_blocks) {
  Ledger ground_truth;
  for (const auto &[addr, amount] : initial_balances) {
    ground_truth.set_balance(addr, amount);
  }
  for (const auto &block : chain_blocks) {
    for (const auto &tx : block.transactions_) {
      ground_truth.apply_transaction(tx);
    }
  }
  return ground_truth;
}

} // namespace


TEST(NodeReorg, NoTransactionsChainSwapHappensAndLedgerUntouched) {
  TestNode node;


  Block losing = make_block(node.chain.latest().hash_, 1000, {}, 0);
  node.chain.add_block(Block(losing));


  Block winning = make_block(node.chain.at(0).hash_, 2000, {}, 4);

  auto result = node.try_reorg(winning);

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 1u);
  EXPECT_EQ((*result)[0], winning.hash_);

  ASSERT_EQ(node.chain.size(), 2u);
  EXPECT_EQ(node.chain.latest().hash_, winning.hash_);
  EXPECT_FALSE(node.orphan_pool.has_orphan(losing.hash_));
  EXPECT_EQ(node.mempool.size(), 0u);
}

TEST(NodeReorg, LedgerMatchesRecomputeFromGenesisAfterReorg) {
  TestNode node;
  Wallet alice = make_wallet();
  Wallet bob = make_wallet();
  Wallet carol = make_wallet();

  node.ledger.set_balance(alice.address, 1000);
  node.ledger.set_balance(bob.address, 1000);
  node.ledger.set_balance(carol.address, 1000);

  const HashBytes genesis_hash = node.chain.at(0).hash_;

  Transaction losing_tx1 = make_signed_tx(alice, bob.address, 50);
  Transaction losing_tx2 = make_signed_tx(bob, carol.address, 30);
  Block losing_block1 = make_block(genesis_hash, 1000, {losing_tx1}, 1);
  Block losing_block2 =
      make_block(losing_block1.hash_, 1100, {losing_tx2}, 1);

  ASSERT_TRUE(node.ledger.apply_transaction(losing_tx1));
  ASSERT_TRUE(node.ledger.apply_transaction(losing_tx2));
  node.chain.add_block(Block(losing_block1));
  node.chain.add_block(Block(losing_block2));

  ASSERT_EQ(*node.ledger.get_balance(alice.address), 950u);
  ASSERT_EQ(*node.ledger.get_balance(bob.address), 1020u);
  ASSERT_EQ(*node.ledger.get_balance(carol.address), 1030u);

  Transaction winning_tx1 = make_signed_tx(alice, carol.address, 40);
  Transaction winning_tx2 = make_signed_tx(carol, bob.address, 10);
  Block winning_block1 = make_block(genesis_hash, 2000, {winning_tx1}, 2);
  Block winning_candidate =
      make_block(winning_block1.hash_, 2100, {winning_tx2}, 2);

  node.orphan_pool.add_orphan(Block(winning_block1));

  auto result = node.try_reorg(winning_candidate);
  ASSERT_TRUE(result.has_value())
      << "heavier fork (work 8 > 4) must be accepted";
  ASSERT_EQ(result->size(), 2u);
  EXPECT_EQ((*result)[0], winning_block1.hash_);
  EXPECT_EQ((*result)[1], winning_candidate.hash_);

  ASSERT_EQ(node.chain.size(), 3u);
  EXPECT_EQ(node.chain.at(1).hash_, winning_block1.hash_);
  EXPECT_EQ(node.chain.at(2).hash_, winning_candidate.hash_);

  Ledger ground_truth = recompute_ledger_from_genesis(
      {{alice.address, 1000}, {bob.address, 1000}, {carol.address, 1000}},
      {winning_block1, winning_candidate});
  EXPECT_EQ(node.ledger.get_balance(alice.address),
            ground_truth.get_balance(alice.address))
      << "BUG: try_reorg's forward-apply loop skips a new-branch tx "
         "whenever its hash is in `tx_hashes` -- but tx_hashes is built "
         "from the new branch itself, so this is true for every "
         "new-branch transaction. Nothing from the winning branch is "
         "ever applied; the ledger is stuck at the common ancestor's "
         "balances instead of reflecting the newly-canonical chain.";
  EXPECT_EQ(node.ledger.get_balance(bob.address),
            ground_truth.get_balance(bob.address));
  EXPECT_EQ(node.ledger.get_balance(carol.address),
            ground_truth.get_balance(carol.address));

  EXPECT_EQ(*node.ledger.get_balance(alice.address), 960u);
  EXPECT_EQ(*node.ledger.get_balance(bob.address), 1010u);
  EXPECT_EQ(*node.ledger.get_balance(carol.address), 1030u);

  uint64_t total = *node.ledger.get_balance(alice.address) +
                    *node.ledger.get_balance(bob.address) +
                    *node.ledger.get_balance(carol.address);
  EXPECT_EQ(total, 3000u);

  EXPECT_TRUE(node.mempool.has_transaction(losing_tx1.compute_hash()));
  EXPECT_TRUE(node.mempool.has_transaction(losing_tx2.compute_hash()));
  EXPECT_EQ(node.mempool.size(), 2u);

  EXPECT_TRUE(node.orphan_pool.has_orphan(winning_block1.hash_));
}

TEST(NodeReorg, SharedTransactionBetweenBranchesIsNotDoubleCounted) {
  TestNode node;
  Wallet alice = make_wallet();
  Wallet bob = make_wallet();

  node.ledger.set_balance(alice.address, 1000);
  node.ledger.set_balance(bob.address, 0);

  const HashBytes genesis_hash = node.chain.at(0).hash_;

  Transaction shared_tx = make_signed_tx(alice, bob.address, 100);

  Transaction losing_only_tx = make_signed_tx(alice, "someone-else", 25);
  Block losing_block =
      make_block(genesis_hash, 1000, {shared_tx, losing_only_tx}, 1);

  ASSERT_TRUE(node.ledger.apply_transaction(shared_tx));
  ASSERT_TRUE(node.ledger.apply_transaction(losing_only_tx));
  node.chain.add_block(Block(losing_block));
  ASSERT_EQ(*node.ledger.get_balance(alice.address), 875u);

  Block winning_candidate =
      make_block(genesis_hash, 2000, {shared_tx}, 3);

  auto result = node.try_reorg(winning_candidate);
  ASSERT_TRUE(result.has_value());

  EXPECT_EQ(*node.ledger.get_balance(alice.address), 900u);
  EXPECT_EQ(*node.ledger.get_balance(bob.address), 100u);


  EXPECT_TRUE(node.mempool.has_transaction(losing_only_tx.compute_hash()));

  EXPECT_FALSE(node.mempool.has_transaction(shared_tx.compute_hash()));
}

TEST(NodeReorg, WinningBranchTransactionThatLedgerCannotAffordDoesNotDesync) {
  TestNode node;
  Wallet alice = make_wallet();
  Wallet bob = make_wallet();

  node.ledger.set_balance(alice.address, 10);
  node.ledger.set_balance(bob.address, 0);

  const HashBytes genesis_hash = node.chain.at(0).hash_;

  Block losing_block = make_block(genesis_hash, 1000, {}, 1);
  node.chain.add_block(Block(losing_block));

  Transaction unaffordable_tx = make_signed_tx(alice, bob.address, 500);
  Block winning_candidate =
      make_block(genesis_hash, 2000, {unaffordable_tx}, /*diff=*/4);

  auto result = node.try_reorg(winning_candidate);

  if (!result.has_value()) {
    EXPECT_EQ(node.chain.latest().hash_, losing_block.hash_);
    EXPECT_EQ(*node.ledger.get_balance(alice.address), 10u);
    EXPECT_EQ(*node.ledger.get_balance(bob.address), 0u);
    return;
  }

  EXPECT_EQ(node.chain.latest().hash_, winning_candidate.hash_);
  EXPECT_EQ(*node.ledger.get_balance(alice.address), 10u)
      << "BUG: try_reorg's forward-apply loop ignores "
         "ledger_.apply_transaction()'s return value. Blockchain now "
         "reports unaffordable_tx as mined on the canonical chain, but "
         "Ledger silently failed to apply it -- alice's balance is stuck at "
         "its pre-reorg value while Blockchain/Mempool have already moved "
         "on. Fix: check the apply_transaction() result in the forward loop "
         "and abort/roll back the whole reorg (mirroring "
         "apply_block_to_ledger's own partial-failure rollback) instead of "
         "unconditionally returning new_hashes.";
  EXPECT_EQ(*node.ledger.get_balance(bob.address), 0u);
}

TEST(NodeReorg, LighterForkIsRejectedAndNothingChanges) {
  TestNode node;
  Wallet alice = make_wallet();
  node.ledger.set_balance(alice.address, 1000);

  const HashBytes genesis_hash = node.chain.at(0).hash_;

  Transaction canonical_tx = make_signed_tx(alice, "bob-address", 100);
  Block canonical_block = make_block(genesis_hash, 1000, {canonical_tx}, 5);
  ASSERT_TRUE(node.ledger.apply_transaction(canonical_tx));
  node.chain.add_block(Block(canonical_block));

  Block lighter_candidate = make_block(genesis_hash, 2000, {},0);

  auto result = node.try_reorg(lighter_candidate);

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(node.chain.size(), 2u);
  EXPECT_EQ(node.chain.latest().hash_, canonical_block.hash_);
  EXPECT_EQ(*node.ledger.get_balance(alice.address), 900u);
  EXPECT_EQ(node.mempool.size(), 0u);
}


TEST(NodeReorg, DisconnectedCandidateIsRejectedAndNothingChanges) {
  TestNode node;
  Wallet alice = make_wallet();
  node.ledger.set_balance(alice.address, 1000);

  HashBytes unknown_hash{};
  unknown_hash.fill(0xAB);
  Block dangling_candidate = make_block(unknown_hash, 3000, {}, 10);

  auto result = node.try_reorg(dangling_candidate);

  EXPECT_FALSE(result.has_value());
  EXPECT_EQ(node.chain.size(), 1u);
  EXPECT_EQ(*node.ledger.get_balance(alice.address), 1000u);
  EXPECT_EQ(node.mempool.size(), 0u);
}

TEST(NodeReorg, MultiHopOrphanForkAppliesLedgerInOldestToNewestOrder) {
  TestNode node;
  Wallet alice = make_wallet();
  Wallet bob = make_wallet();

  node.ledger.set_balance(alice.address, 100);
  node.ledger.set_balance(bob.address, 0);

  const HashBytes genesis_hash = node.chain.at(0).hash_;

  Transaction tx1 = make_signed_tx(alice, bob.address, 100);
  Block block1 = make_block(genesis_hash, 1000, {tx1}, 1);

  Transaction tx2 = make_signed_tx(bob, alice.address, 100);
  Block block2 = make_block(block1.hash_, 1100, {tx2}, 1);

  Transaction tx3 = make_signed_tx(alice, bob.address, 100);
  Block block3 = make_block(block2.hash_, 1200, {tx3}, 1);

  node.orphan_pool.add_orphan(Block(block1));
  node.orphan_pool.add_orphan(Block(block2));


  auto result = node.try_reorg(block3);

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 3u);
  EXPECT_EQ((*result)[0], block1.hash_);
  EXPECT_EQ((*result)[1], block2.hash_);
  EXPECT_EQ((*result)[2], block3.hash_);

  Ledger ground_truth = recompute_ledger_from_genesis(
      {{alice.address, 100}, {bob.address, 0}}, {block1, block2, block3});

  EXPECT_EQ(node.ledger.get_balance(alice.address),
            ground_truth.get_balance(alice.address));
  EXPECT_EQ(node.ledger.get_balance(bob.address),
            ground_truth.get_balance(bob.address));
  EXPECT_EQ(*node.ledger.get_balance(alice.address), 0u);
  EXPECT_EQ(*node.ledger.get_balance(bob.address), 100u);
}


TEST(NodeReorg, ReorgFromForkPointDeeperThanGenesisPreservesCommonPrefix) {
  TestNode node;
  Wallet alice = make_wallet();
  Wallet bob = make_wallet();
  node.ledger.set_balance(alice.address, 1000);
  node.ledger.set_balance(bob.address, 0);


  Transaction common_tx1 = make_signed_tx(alice, bob.address, 10);
  Block common_block1 =
      make_block(node.chain.at(0).hash_, 500, {common_tx1}, 1);
  ASSERT_TRUE(node.ledger.apply_transaction(common_tx1));
  node.chain.add_block(Block(common_block1));

  Transaction common_tx2 = make_signed_tx(bob, alice.address, 5);
  Block common_block2 = make_block(common_block1.hash_, 600, {common_tx2}, 1);
  ASSERT_TRUE(node.ledger.apply_transaction(common_tx2));
  node.chain.add_block(Block(common_block2));

  ASSERT_EQ(*node.ledger.get_balance(alice.address), 995u);
  ASSERT_EQ(*node.ledger.get_balance(bob.address), 5u);

  Transaction losing_tx = make_signed_tx(alice, "carol-address", 50);
  Block losing_tip = make_block(common_block2.hash_, 700, {losing_tx}, 1);
  ASSERT_TRUE(node.ledger.apply_transaction(losing_tx));
  node.chain.add_block(Block(losing_tip));
  ASSERT_EQ(*node.ledger.get_balance(alice.address), 945u);


  Transaction winning_tx = make_signed_tx(alice, bob.address, 200);
  Block winning_candidate =
      make_block(common_block2.hash_, 800, {winning_tx},3);

  auto result = node.try_reorg(winning_candidate);
  ASSERT_TRUE(result.has_value());
  ASSERT_EQ(result->size(), 1u);
  EXPECT_EQ((*result)[0], winning_candidate.hash_);

  ASSERT_EQ(node.chain.size(), 4u);
  EXPECT_EQ(node.chain.at(1).hash_, common_block1.hash_);
  EXPECT_EQ(node.chain.at(2).hash_, common_block2.hash_);
  EXPECT_EQ(node.chain.at(3).hash_, winning_candidate.hash_);

  EXPECT_EQ(*node.ledger.get_balance(alice.address), 795u);
  EXPECT_EQ(*node.ledger.get_balance(bob.address), 205u);

  EXPECT_TRUE(node.mempool.has_transaction(losing_tx.compute_hash()));
  EXPECT_EQ(node.mempool.size(), 1u);
}
