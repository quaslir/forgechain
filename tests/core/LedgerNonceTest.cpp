// Nonces are what stop a signed transaction from being replayed: a
// transaction is only valid at exactly one point in its sender's history.
// These tests pin down that point, and that a reorg can undo it.

#include "core/Ledger.hpp"
#include "core/Transaction.hpp"
#include "crypto/CommonTypes.hpp"

#include <gtest/gtest.h>

#include <cstdint>

using namespace forgechain::core;
using forgechain::crypto::bytes;
using forgechain::crypto::str;

namespace {

Transaction tx_from(const str &sender, const str &recipient, uint64_t amount,
                    uint64_t nonce, uint64_t fee = 0) {
  return Transaction{sender, recipient, amount, bytes{0x01}, fee, nonce};
}

} // namespace

TEST(LedgerNonce, StartsAtZeroForUnknownAddress) {
  Ledger ledger;
  EXPECT_EQ(ledger.next_nonce("alice"), 0u);
}

TEST(LedgerNonce, ReceivingFundsDoesNotAdvanceNonce) {
  Ledger ledger;
  ledger.set_balance("alice", 100);
  ASSERT_TRUE(ledger.apply_transaction(tx_from("alice", "bob", 10, 0)));

  EXPECT_EQ(ledger.next_nonce("bob"), 0u)
      << "the nonce counts what an address sent, not what it received";
}

TEST(LedgerNonce, FirstTransactionMustUseNonceZero) {
  Ledger ledger;
  ledger.set_balance("alice", 100);

  EXPECT_FALSE(ledger.apply_transaction(tx_from("alice", "bob", 10, 1)));
  EXPECT_FALSE(ledger.apply_transaction(tx_from("alice", "bob", 10, 42)));
  EXPECT_EQ(ledger.get_balance("alice"), 100u);

  EXPECT_TRUE(ledger.apply_transaction(tx_from("alice", "bob", 10, 0)));
  EXPECT_EQ(ledger.next_nonce("alice"), 1u);
}

TEST(LedgerNonce, ReplayOfTheSameTransactionIsRejected) {
  Ledger ledger;
  ledger.set_balance("alice", 100);
  Transaction tx = tx_from("alice", "bob", 30, 0);

  ASSERT_TRUE(ledger.apply_transaction(tx));
  EXPECT_FALSE(ledger.apply_transaction(tx));
  EXPECT_FALSE(ledger.apply_transaction(tx));

  EXPECT_EQ(ledger.get_balance("alice"), 70u);
  EXPECT_EQ(ledger.get_balance("bob"), 30u);
}

TEST(LedgerNonce, GapInSequenceIsRejectedUntilFilled) {
  Ledger ledger;
  ledger.set_balance("alice", 100);
  ASSERT_TRUE(ledger.apply_transaction(tx_from("alice", "bob", 10, 0)));

  EXPECT_FALSE(ledger.apply_transaction(tx_from("alice", "bob", 10, 2)));
  EXPECT_TRUE(ledger.apply_transaction(tx_from("alice", "bob", 10, 1)));
  EXPECT_TRUE(ledger.apply_transaction(tx_from("alice", "bob", 10, 2)));
  EXPECT_EQ(ledger.next_nonce("alice"), 3u);
}

TEST(LedgerNonce, IdenticalPaymentsDifferByNonce) {
  Ledger ledger;
  ledger.set_balance("alice", 100);
  Transaction first = tx_from("alice", "bob", 10, 0);
  Transaction second = tx_from("alice", "bob", 10, 1);

  EXPECT_NE(first.compute_hash(), second.compute_hash())
      << "paying the same amount twice must produce two distinct transactions";
  EXPECT_TRUE(ledger.apply_transaction(first));
  EXPECT_TRUE(ledger.apply_transaction(second));
  EXPECT_EQ(ledger.get_balance("bob"), 20u);
}

TEST(LedgerNonce, NoncesAreTrackedPerSender) {
  Ledger ledger;
  ledger.set_balance("alice", 100);
  ledger.set_balance("bob", 100);
  ASSERT_TRUE(ledger.apply_transaction(tx_from("alice", "carol", 10, 0)));
  ASSERT_TRUE(ledger.apply_transaction(tx_from("alice", "carol", 10, 1)));

  EXPECT_EQ(ledger.next_nonce("alice"), 2u);
  EXPECT_EQ(ledger.next_nonce("bob"), 0u);
  EXPECT_TRUE(ledger.apply_transaction(tx_from("bob", "carol", 10, 0)));
}

TEST(LedgerNonce, ReverseRestoresTheNonce) {
  Ledger ledger;
  ledger.set_balance("alice", 100);
  Transaction tx = tx_from("alice", "bob", 30, 0, 5);
  ASSERT_TRUE(ledger.apply_transaction(tx));

  ASSERT_TRUE(ledger.reverse_transaction(tx));

  EXPECT_EQ(ledger.next_nonce("alice"), 0u);
  EXPECT_EQ(ledger.get_balance("alice"), 100u);
}

TEST(LedgerNonce, TransactionCanBeReappliedAfterReversal) {
  Ledger ledger;
  ledger.set_balance("alice", 100);
  Transaction t0 = tx_from("alice", "bob", 10, 0);
  Transaction t1 = tx_from("alice", "bob", 20, 1);
  ASSERT_TRUE(ledger.apply_transaction(t0));
  ASSERT_TRUE(ledger.apply_transaction(t1));

  ASSERT_TRUE(ledger.reverse_transaction(t1));
  ASSERT_TRUE(ledger.reverse_transaction(t0));
  EXPECT_EQ(ledger.next_nonce("alice"), 0u);

  EXPECT_TRUE(ledger.apply_transaction(t0));
  EXPECT_TRUE(ledger.apply_transaction(t1));
  EXPECT_EQ(ledger.get_balance("alice"), 70u);
  EXPECT_EQ(ledger.get_balance("bob"), 30u);
}

TEST(LedgerNonce, ReversingOutOfOrderIsRejected) {
  Ledger ledger;
  ledger.set_balance("alice", 100);
  Transaction t0 = tx_from("alice", "bob", 10, 0);
  Transaction t1 = tx_from("alice", "bob", 20, 1);
  ASSERT_TRUE(ledger.apply_transaction(t0));
  ASSERT_TRUE(ledger.apply_transaction(t1));

  EXPECT_FALSE(ledger.reverse_transaction(t0));
  EXPECT_EQ(ledger.next_nonce("alice"), 2u);
}

TEST(LedgerNonce, ReversingSomethingNeverAppliedIsRejected) {
  Ledger ledger;
  ledger.set_balance("alice", 100);
  ledger.set_balance("bob", 0);

  EXPECT_FALSE(ledger.reverse_transaction(tx_from("alice", "bob", 0, 0)));
  EXPECT_EQ(ledger.next_nonce("alice"), 0u);
}

TEST(LedgerNonce, CoinbaseIgnoresNonces) {
  Ledger ledger;
  Transaction coinbase{kCoinbaseSender, "miner", 50, bytes{}, 0, 0};

  ASSERT_TRUE(ledger.apply_transaction(coinbase));
  EXPECT_EQ(ledger.next_nonce(kCoinbaseSender), 0u)
      << "the coinbase sender has no nonce sequence to advance";
  EXPECT_TRUE(ledger.apply_transaction(coinbase))
      << "every block carries a coinbase with the same shape";
  EXPECT_EQ(ledger.get_balance("miner"), 100u);
}

TEST(LedgerNonce, CoinbaseCanBeReversed) {
  Ledger ledger;
  Transaction coinbase{kCoinbaseSender, "miner", 50, bytes{}, 0, 0};
  ASSERT_TRUE(ledger.apply_transaction(coinbase));

  EXPECT_TRUE(ledger.reverse_transaction(coinbase));
  EXPECT_EQ(ledger.get_balance("miner").value_or(0), 0u);
}
