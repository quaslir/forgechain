// BlockTemplate is what the miner builds on. Every field must come from one
// consistent view of the chain, and a block mined straight from a template
// must always be accepted -- including when many blocks land in one second.

#define private public
#include "chain/ChainManager.hpp"
#undef private

#include "consensus/ConsensusParams.hpp"
#include "consensus/ProofOfWork.hpp"
#include "core/Block.hpp"
#include "core/Transaction.hpp"
#include "crypto/CommonTypes.hpp"
#include "crypto/Address.hpp"
#include "crypto/Keys.hpp"
#include "crypto/Signature.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>

using namespace forgechain::chain;
using namespace forgechain::core;
using namespace forgechain::crypto;
using forgechain::consensus::ConsensusParams;
using forgechain::consensus::median_time_past;
using forgechain::consensus::mine_block;

namespace {

constexpr size_t kMempoolSize = 1000;
constexpr uint64_t kNow = 1'700'000'000;

const ConsensusParams kParams{.initial_difficulty = 4,
                              .min_difficulty = 1,
                              .target_block_time = 10,
                              .retarget_interval = 20,
                              .mtp_window = 11,
                              .max_future_drift = 120};

using Status = BlockOutcome::Status;

struct Fixture {
  uint64_t now{kNow};
  ChainManager manager;

  explicit Fixture(const ConsensusParams &params = kParams)
      : manager(kMempoolSize, params, nullptr, [this] { return now; }) {}

  Block mine_from_template(size_t max_txs = 50) {
    auto tmpl = manager.block_template(max_txs);
    return mine_block(1, tmpl.prev_hash, tmpl.timestamp, tmpl.difficulty,
                      tmpl.transactions);
  }
};

struct TestWallet {
  KeyPair keys;
  str address;
};

TestWallet make_wallet() {
  KeyPair kp = generate_keypair();
  return TestWallet{kp, derive_address(kp.public_key)};
}

Transaction make_signed_tx(const TestWallet &sender, const str &recipient,
                           uint64_t amount) {
  Transaction tx(sender.address, recipient, amount, sender.keys.public_key, 0);
  tx.signature_ = sign(tx.serialize_for_signing(), sender.keys.private_key);
  return tx;
}

} // namespace

TEST(ChainManagerBlockTemplate, BuildsOnCurrentTip) {
  Fixture f;
  auto tmpl = f.manager.block_template(50);

  EXPECT_EQ(tmpl.prev_hash, f.manager.latest_hash());
  EXPECT_EQ(tmpl.height, f.manager.chain_height());
  EXPECT_EQ(tmpl.difficulty, f.manager.next_block_difficulty());
}

TEST(ChainManagerBlockTemplate, TimestampIsClockWhenClockIsAhead) {
  Fixture f;
  EXPECT_EQ(f.manager.block_template(50).timestamp, kNow);
}

TEST(ChainManagerBlockTemplate, TimestampIsMtpPlusOneWhenClockIsBehind) {
  Fixture f;
  for (int i = 0; i < 3; i++) {
    ASSERT_EQ(f.manager.submit_block(f.mine_from_template()).status,
              Status::Accepted);
  }
  f.now = kNow - 1000;

  auto tmpl = f.manager.block_template(50);
  uint64_t mtp = median_time_past(
      tmpl.height, kParams.mtp_window,
      [&](size_t h) -> const Block & { return f.manager.blockchain_.at(h); });

  EXPECT_EQ(tmpl.timestamp, mtp + 1);
  EXPECT_GT(tmpl.timestamp, f.now);
}

TEST(ChainManagerBlockTemplate, ManyBlocksInOneSecondAreAllAccepted) {
  Fixture f;
  for (int i = 0; i < 30; i++) {
    Block block = f.mine_from_template();
    ASSERT_EQ(f.manager.submit_block(block).status, Status::Accepted)
        << "block " << i << " mined from a template was rejected";
  }
  EXPECT_EQ(f.manager.chain_height(), 31u);
}

TEST(ChainManagerBlockTemplate, FollowsRetargetAcrossEpochs) {
  const ConsensusParams params{.initial_difficulty = 1,
                               .min_difficulty = 1,
                               .target_block_time = 10,
                               .retarget_interval = 4,
                               .mtp_window = 11,
                               .max_future_drift = 120};
  Fixture f(params);
  for (int i = 0; i < 16; i++) {
    ASSERT_EQ(f.manager.submit_block(f.mine_from_template()).status,
              Status::Accepted)
        << "height " << f.manager.chain_height();
    f.now += 1;
  }
  EXPECT_GT(f.manager.next_block_difficulty(), params.initial_difficulty);
}

TEST(ChainManagerBlockTemplate, TransactionsAreFilteredByLedger) {
  Fixture f;
  TestWallet alice = make_wallet();
  TestWallet bob = make_wallet();
  f.manager.ledger_.set_balance(alice.address, 10);
  f.manager.ledger_.set_balance(bob.address, 100);
  f.manager.mempool_.add_transaction(make_signed_tx(bob, "carol", 20),
                                     bob.keys.public_key);
  f.manager.mempool_.add_transaction(make_signed_tx(alice, "carol", 500),
                                     alice.keys.public_key);

  auto tmpl = f.manager.block_template(50);

  ASSERT_EQ(tmpl.transactions.size(), 1u);
  EXPECT_EQ(tmpl.transactions[0].sender_, bob.address);
}

TEST(ChainManagerBlockTemplate, RespectsMaxTxs) {
  Fixture f;
  TestWallet alice = make_wallet();
  f.manager.ledger_.set_balance(alice.address, 1000);
  for (int i = 0; i < 5; i++) {
    f.manager.mempool_.add_transaction(
        make_signed_tx(alice, "carol-" + std::to_string(i), 10),
        alice.keys.public_key);
  }

  EXPECT_EQ(f.manager.block_template(3).transactions.size(), 3u);
}

TEST(ChainManagerBlockTemplate, BlockWithTemplateTransactionsIsAccepted) {
  Fixture f;
  TestWallet alice = make_wallet();
  f.manager.ledger_.set_balance(alice.address, 100);
  Transaction tx = make_signed_tx(alice, "carol", 30);
  f.manager.mempool_.add_transaction(tx, alice.keys.public_key);

  Block block = f.mine_from_template();

  ASSERT_EQ(f.manager.submit_block(block).status, Status::Accepted);
  EXPECT_EQ(f.manager.get_balance("carol"), 30u);
  EXPECT_FALSE(f.manager.has_transaction(tx.compute_hash()));
}
