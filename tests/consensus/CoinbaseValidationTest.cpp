#include "consensus/ProofOfWork.hpp"

#include <gtest/gtest.h>
#include "core/Transaction.hpp"
#include <cstdint>
#include <vector>

using namespace forgechain::consensus;
using namespace forgechain::core;

namespace {

Transaction make_regular(const str &sender, uint64_t amount, uint64_t fee) {
  return Transaction(sender, "recipient", amount, {}, fee);
}

Transaction make_coinbase(uint64_t amount) {
  return Transaction(kCoinbaseSender, "miner", amount, {}, 0);
}

}  // namespace

TEST(ValidateCoinbaseAmount, BlockWithNoTransactionsIsValid) {
  std::vector<Transaction> txs;
  EXPECT_TRUE(validate_coinbase_amount(txs));
}

TEST(ValidateCoinbaseAmount, BlockWithNoCoinbaseIsValid) {
  std::vector<Transaction> txs{
      make_regular("alice", 100, 5),
      make_regular("bob", 50, 2),
  };
  EXPECT_TRUE(validate_coinbase_amount(txs));
}

TEST(ValidateCoinbaseAmount, CoinbaseExactlyEqualToRewardWithNoFeesIsValid) {
  std::vector<Transaction> txs{make_coinbase(mining_reward)};
  EXPECT_TRUE(validate_coinbase_amount(txs));
}

TEST(ValidateCoinbaseAmount, CoinbaseEqualToRewardPlusFeesIsValid) {
  std::vector<Transaction> txs{
      make_coinbase(mining_reward + 8),
      make_regular("alice", 100, 5),
      make_regular("bob", 50, 3),
  };
  EXPECT_TRUE(validate_coinbase_amount(txs));
}

TEST(ValidateCoinbaseAmount, CoinbaseBelowRewardPlusFeesIsValid) {
  std::vector<Transaction> txs{
      make_coinbase(mining_reward + 1),
      make_regular("alice", 100, 10),
  };
  EXPECT_TRUE(validate_coinbase_amount(txs));
}

TEST(ValidateCoinbaseAmount, CoinbaseExceedingRewardPlusFeesIsRejected) {
  std::vector<Transaction> txs{
      make_coinbase(mining_reward + 9),
      make_regular("alice", 100, 5),
      make_regular("bob", 50, 3),
  };
  EXPECT_FALSE(validate_coinbase_amount(txs));
}

TEST(ValidateCoinbaseAmount, CoinbaseExceedingBareRewardWithNoFeesIsRejected) {
  std::vector<Transaction> txs{make_coinbase(mining_reward + 1)};
  EXPECT_FALSE(validate_coinbase_amount(txs));
}

TEST(ValidateCoinbaseAmount, MultipleCoinbaseTransactionsAreRejected) {
  std::vector<Transaction> txs{
      make_coinbase(mining_reward),
      make_coinbase(mining_reward),
  };
  EXPECT_FALSE(validate_coinbase_amount(txs));
}

TEST(ValidateCoinbaseAmount, CoinbaseWithForgedNonZeroFeeDoesNotInflateItsOwnAllowance) {
  std::vector<Transaction> txs{
      make_coinbase(mining_reward + 100),  // claims reward + 100
  };
  EXPECT_FALSE(validate_coinbase_amount(txs));
}

TEST(ValidateCoinbaseAmount, ZeroMiningRewardWithOnlyFeesStillValidatesCorrectly) {
  std::vector<Transaction> txs{
      make_coinbase(mining_reward + 15),
      make_regular("alice", 200, 15),
  };
  EXPECT_TRUE(validate_coinbase_amount(txs));
}
