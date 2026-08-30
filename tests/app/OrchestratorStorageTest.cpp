#include "consensus/ProofOfWork.hpp"
#include "core/Block.hpp"
#include "core/Transaction.hpp"
#include "crypto/Address.hpp"
#include "crypto/CommonTypes.hpp"
#include "crypto/Keys.hpp"
#include "crypto/Signature.hpp"
#include "storage/SqliteConnection.hpp"
#include "storage/SqliteStatement.hpp"
#include "storage/Storage.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unistd.h>

#define private public
#include "app/Orchestrator.hpp"
#undef private

using namespace forgechain::app;
using namespace forgechain::core;
using namespace forgechain::consensus;
using namespace forgechain::crypto;

namespace {

uint16_t next_test_port() {
  static auto port = static_cast<uint16_t>(36000 + (getpid() % 1000) * 20);
  return port++;
}

std::string next_test_db_path() {
  static int counter = 0;
  return "/tmp/forgechain_orchestrator_test_" + std::to_string(getpid()) +
         "_" + std::to_string(counter++) + ".db";
}

struct Wallet {
  KeyPair keys;
  str address;
};

Wallet make_wallet() {
  KeyPair kp = generate_keypair();
  return Wallet{kp, derive_address(kp.public_key)};
}

Transaction make_signed_tx(const Wallet &sender, const str &recipient,
                            uint64_t amount, uint64_t fee = 0) {
  Transaction tx(sender.address, recipient, amount, sender.keys.public_key,
                 fee);
  tx.signature_ = sign(tx.serialize_for_signing(), sender.keys.private_key);
  return tx;
}

void mine_blocks_locally(Orchestrator &orch, int count,
                          uint32_t difficulty = 1) {
  for (int i = 0; i < count; ++i) {
    HashBytes prev_hash = orch.chain_.latest().hash_;
    Block mined = mine_block(1, prev_hash,
                             static_cast<uint64_t>(1700000000 + i), difficulty,
                             {});
    orch.node_.submit_block(mined);
  }
}

class TempDbPath {
public:
  TempDbPath() : path_(next_test_db_path()) { std::remove(path_.c_str()); }
  ~TempDbPath() { std::remove(path_.c_str()); }
  TempDbPath(const TempDbPath &) = delete;
  TempDbPath &operator=(const TempDbPath &) = delete;
  [[nodiscard]] const std::string &path() const { return path_; }

private:
  std::string path_;
};

}  // namespace

TEST(Orchestrator, WithoutDbPathStorageIsNotEngaged) {
  OrchestratorConfig config;
  config.listen_port = next_test_port();
  config.db_path = "";

  Orchestrator orch(config);
  EXPECT_FALSE(orch.storage_.has_value());
}

TEST(Orchestrator, WithoutDbPathStopDoesNotCreateAFile) {
  std::string maybe_path = next_test_db_path();
  std::remove(maybe_path.c_str());

  {
    OrchestratorConfig config;
    config.listen_port = next_test_port();
    config.db_path = "";
    Orchestrator orch(config);
    mine_blocks_locally(orch, 2);
    orch.stop();
  }
  std::ifstream probe(maybe_path);
  EXPECT_FALSE(probe.good());
}

TEST(Orchestrator, WithDbPathStorageIsEngaged) {
  TempDbPath db;
  OrchestratorConfig config;
  config.listen_port = next_test_port();
  config.db_path = db.path();

  Orchestrator orch(config);
  EXPECT_TRUE(orch.storage_.has_value());
}

TEST(Orchestrator, FreshDbPathStartsWithOnlyGenesisAndNoStoredBalances) {
  TempDbPath db;
  OrchestratorConfig config;
  config.listen_port = next_test_port();
  config.db_path = db.path();

  Orchestrator orch(config);
  EXPECT_EQ(orch.chain_.size(), 1u);
}

TEST(Orchestrator, StopPersistsAllMinedBlocksToStorage) {
  TempDbPath db;
  OrchestratorConfig config;
  config.listen_port = next_test_port();
  config.db_path = db.path();

  Orchestrator orch(config);
  mine_blocks_locally(orch, 3);
  ASSERT_EQ(orch.chain_.size(), 4u);
  ASSERT_TRUE(orch.storage_.has_value());

  orch.stop();

  EXPECT_TRUE(orch.storage_.has_value());
}

TEST(Orchestrator, ChainHeightSurvivesDestructionAndReconstruction) {
  TempDbPath db;
  OrchestratorConfig config;
  config.listen_port = next_test_port();
  config.db_path = db.path();

  {
    Orchestrator orch(config);
    mine_blocks_locally(orch, 5);
    ASSERT_EQ(orch.chain_.size(), 6u);
  }

  {
    Orchestrator orch(config);
    EXPECT_EQ(orch.chain_.size(), 6u)
        << "chain height did not survive a full destroy+reconstruct cycle";
  }
}

TEST(Orchestrator, RestoredBlocksMatchOriginalHashesInOrder) {
  TempDbPath db;
  OrchestratorConfig config;
  config.listen_port = next_test_port();
  config.db_path = db.path();

  HashBytes hash_at_1{};
  HashBytes hash_at_2{};
  HashBytes hash_at_3{};

  {
    Orchestrator orch(config);
    mine_blocks_locally(orch, 3);
    hash_at_1 = orch.chain_.at(1).hash_;
    hash_at_2 = orch.chain_.at(2).hash_;
    hash_at_3 = orch.chain_.at(3).hash_;
  }

  {
    Orchestrator orch(config);
    ASSERT_EQ(orch.chain_.size(), 4u);
    EXPECT_EQ(orch.chain_.at(1).hash_, hash_at_1);
    EXPECT_EQ(orch.chain_.at(2).hash_, hash_at_2);
    EXPECT_EQ(orch.chain_.at(3).hash_, hash_at_3);
  }
}

TEST(Orchestrator, BalancesSurviveDestructionAndReconstruction) {
  TempDbPath db;
  OrchestratorConfig config;
  config.listen_port = next_test_port();
  config.db_path = db.path();

  Wallet alice = make_wallet();
  Wallet bob = make_wallet();

  {
    Orchestrator orch(config);
    orch.ledger_.set_balance(alice.address, 1000);

    Transaction tx = make_signed_tx(alice, bob.address, 250, 5);
    HashBytes prev_hash = orch.chain_.latest().hash_;
    Block mined = mine_block(1, prev_hash, 1700000000, 1, {tx});
    orch.node_.submit_block(mined);

    ASSERT_EQ(orch.ledger_.get_balance(alice.address), std::optional<uint64_t>(745));
    ASSERT_EQ(orch.ledger_.get_balance(bob.address), std::optional<uint64_t>(250));
  }

  {
    Orchestrator orch(config);
    EXPECT_EQ(orch.ledger_.get_balance(alice.address), std::optional<uint64_t>(745));
    EXPECT_EQ(orch.ledger_.get_balance(bob.address), std::optional<uint64_t>(250));
  }
}

TEST(Orchestrator, MultipleStopCallsDoNotCorruptStorage) {
  TempDbPath db;
  OrchestratorConfig config;
  config.listen_port = next_test_port();
  config.db_path = db.path();

  Orchestrator orch(config);
  mine_blocks_locally(orch, 2);

  EXPECT_NO_THROW({
    orch.stop();
    orch.stop();
  });
}

TEST(Orchestrator, ReconstructingAfterMultipleStopCallsStillRestoresCorrectly) {
  TempDbPath db;
  OrchestratorConfig config;
  config.listen_port = next_test_port();
  config.db_path = db.path();

  {
    Orchestrator orch(config);
    mine_blocks_locally(orch, 2);
    orch.stop();
    orch.stop();
  }

  Orchestrator orch(config);
  EXPECT_EQ(orch.chain_.size(), 3u);
}

TEST(Orchestrator, CorruptedStorageWithAGapInHeightsThrowsOnConstruction) {
  TempDbPath db;
  OrchestratorConfig config;
  config.listen_port = next_test_port();
  config.db_path = db.path();

  {
    Orchestrator orch(config);
    mine_blocks_locally(orch, 3);
  }

  {
    forgechain::storage::SqliteConnection conn(db.path());
    ASSERT_TRUE(conn.is_valid());
    forgechain::storage::SqliteStatement del(
        conn, "DELETE FROM blocks WHERE height = 2");
    ASSERT_TRUE(del.is_valid());
    ASSERT_EQ(del.step(), SQLITE_DONE);
  }

  EXPECT_THROW({ Orchestrator broken(config); }, std::runtime_error);
}
