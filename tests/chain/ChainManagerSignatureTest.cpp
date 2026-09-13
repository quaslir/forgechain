// Transactions inside a block carry no privilege: a block is only accepted
// if every non-coinbase transaction in it is signed by the sender whose
// funds it moves. Without this, a miner could include forged spends and
// every node would apply them.

#define private public
#include "chain/ChainManager.hpp"
#undef private

#include "consensus/ConsensusParams.hpp"
#include "consensus/ProofOfWork.hpp"
#include "core/Block.hpp"
#include "core/Transaction.hpp"
#include "crypto/Address.hpp"
#include "crypto/CommonTypes.hpp"
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
using forgechain::consensus::mine_block;
using forgechain::consensus::mining_reward;

namespace {

constexpr size_t kMempoolSize = 1000;
constexpr uint64_t kNow = 1'700'000'000;
constexpr uint32_t kDifficulty = 4;

const ConsensusParams kParams{.initial_difficulty = kDifficulty,
                              .min_difficulty = 1,
                              .target_block_time = 10,
                              .retarget_interval = 20,
                              .mtp_window = 11,
                              .max_future_drift = 120};

using Status = BlockOutcome::Status;

struct TestWallet {
  KeyPair keys;
  str address;
};

TestWallet make_wallet() {
  KeyPair kp = generate_keypair();
  return TestWallet{kp, derive_address(kp.public_key)};
}

Transaction signed_tx(const TestWallet &sender, const str &recipient,
                      uint64_t amount, uint64_t fee = 0) {
  Transaction tx(sender.address, recipient, amount, sender.keys.public_key,
                 fee);
  tx.signature_ = sign(tx.serialize_for_signing(), sender.keys.private_key);
  return tx;
}

struct Fixture {
  uint64_t now{kNow};
  ChainManager manager;

  Fixture() : manager(kMempoolSize, kParams, nullptr, [this] { return now; }) {}

  Block block_with(std::vector<Transaction> txs) {
    return mine_block(1, manager.latest_hash(), now, kDifficulty,
                      std::move(txs));
  }
};

} // namespace

TEST(ChainManagerSignature, RejectsUnsignedTransactionInBlock) {
  Fixture f;
  TestWallet victim = make_wallet();
  TestWallet miner = make_wallet();
  f.manager.set_balance(victim.address, 1000);

  Transaction forged(victim.address, miner.address, 1000, bytes{}, 0);

  auto outcome = f.manager.submit_block(f.block_with({forged}));

  EXPECT_EQ(outcome.status, Status::Rejected);
  EXPECT_EQ(f.manager.get_balance(victim.address), 1000u);
  EXPECT_EQ(f.manager.get_balance(miner.address).value_or(0), 0u);
  EXPECT_EQ(f.manager.chain_height(), 1u);
}

TEST(ChainManagerSignature, RejectsSignatureFromAnotherKey) {
  Fixture f;
  TestWallet victim = make_wallet();
  TestWallet attacker = make_wallet();
  f.manager.set_balance(victim.address, 1000);

  Transaction forged = signed_tx(attacker, attacker.address, 1000);
  forged.sender_ = victim.address;

  EXPECT_EQ(f.manager.submit_block(f.block_with({forged})).status,
            Status::Rejected);
  EXPECT_EQ(f.manager.get_balance(victim.address), 1000u);
}

TEST(ChainManagerSignature, RejectsPublicKeyThatIsNotTheSender) {
  Fixture f;
  TestWallet victim = make_wallet();
  TestWallet attacker = make_wallet();
  f.manager.set_balance(victim.address, 1000);

  Transaction forged(victim.address, attacker.address, 1000,
                     attacker.keys.public_key, 0);
  forged.signature_ =
      sign(forged.serialize_for_signing(), attacker.keys.private_key);

  EXPECT_EQ(f.manager.submit_block(f.block_with({forged})).status,
            Status::Rejected);
  EXPECT_EQ(f.manager.get_balance(victim.address), 1000u);
}

TEST(ChainManagerSignature, RejectsTamperedAmount) {
  Fixture f;
  TestWallet alice = make_wallet();
  f.manager.set_balance(alice.address, 1000);

  Transaction tx = signed_tx(alice, "bob", 10);
  tx.amount_ = 900;

  EXPECT_EQ(f.manager.submit_block(f.block_with({tx})).status,
            Status::Rejected);
  EXPECT_EQ(f.manager.get_balance(alice.address), 1000u);
}

TEST(ChainManagerSignature, AcceptsProperlySignedTransaction) {
  Fixture f;
  TestWallet alice = make_wallet();
  f.manager.set_balance(alice.address, 1000);

  Transaction tx = signed_tx(alice, "bob", 300, 5);

  EXPECT_EQ(f.manager.submit_block(f.block_with({tx})).status,
            Status::Accepted);
  EXPECT_EQ(f.manager.get_balance(alice.address), 695u);
  EXPECT_EQ(f.manager.get_balance("bob"), 300u);
}

TEST(ChainManagerSignature, CoinbaseNeedsNoSignature) {
  Fixture f;
  TestWallet miner = make_wallet();
  Transaction coinbase(kCoinbaseSender, miner.address, mining_reward, bytes{},
                       0);

  EXPECT_EQ(f.manager.submit_block(f.block_with({coinbase})).status,
            Status::Accepted);
  EXPECT_EQ(f.manager.get_balance(miner.address), mining_reward);
}

TEST(ChainManagerSignature, ForgedTransactionAfterCoinbaseRollsBackTheBlock) {
  Fixture f;
  TestWallet victim = make_wallet();
  TestWallet miner = make_wallet();
  f.manager.set_balance(victim.address, 1000);
  Transaction coinbase(kCoinbaseSender, miner.address, mining_reward, bytes{},
                       0);
  Transaction forged(victim.address, miner.address, 1000, bytes{}, 0);

  auto outcome = f.manager.submit_block(f.block_with({coinbase, forged}));

  EXPECT_EQ(outcome.status, Status::Rejected);
  EXPECT_EQ(f.manager.get_balance(miner.address).value_or(0), 0u);
  EXPECT_EQ(f.manager.get_balance(victim.address), 1000u);
}

TEST(ChainManagerSignature, ValidTransactionBeforeForgedOneIsRolledBack) {
  Fixture f;
  TestWallet alice = make_wallet();
  TestWallet victim = make_wallet();
  TestWallet miner = make_wallet();
  f.manager.set_balance(alice.address, 1000);
  f.manager.set_balance(victim.address, 1000);

  Transaction good = signed_tx(alice, "bob", 100);
  Transaction forged(victim.address, miner.address, 500, bytes{}, 0);

  EXPECT_EQ(f.manager.submit_block(f.block_with({good, forged})).status,
            Status::Rejected);
  EXPECT_EQ(f.manager.get_balance(alice.address), 1000u);
  EXPECT_EQ(f.manager.get_balance("bob").value_or(0), 0u);
  EXPECT_EQ(f.manager.get_balance(victim.address), 1000u);
}

TEST(ChainManagerSignature, ForgedTransactionInForkBranchIsRejected) {
  Fixture f;
  TestWallet victim = make_wallet();
  TestWallet miner = make_wallet();
  f.manager.set_balance(victim.address, 1000);
  const HashBytes genesis = f.manager.blockchain_.at(0).hash_;

  Block honest = mine_block(1, genesis, kNow, kDifficulty, {});
  ASSERT_EQ(f.manager.submit_block(honest).status, Status::Accepted);

  Transaction forged(victim.address, miner.address, 1000, bytes{}, 0);
  Block b1 = mine_block(1, genesis, kNow + 1, kDifficulty, {});
  Block b2 = mine_block(1, b1.hash_, kNow + 2, kDifficulty, {forged});
  f.manager.submit_block(b1);
  auto outcome = f.manager.submit_block(b2);

  EXPECT_NE(outcome.status, Status::Reorged);
  EXPECT_EQ(f.manager.latest_hash(), honest.hash_);
  EXPECT_EQ(f.manager.get_balance(victim.address), 1000u);
}
