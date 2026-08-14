#include "core/Ledger.hpp"
#include "core/Transaction.hpp"

#include <gtest/gtest.h>
#include <optional>
#include <cstdint>
using namespace forgechain::core;

TEST(Ledger, UnknownAddressReturnsNullopt) {
    Ledger ledger;
    EXPECT_EQ(ledger.get_balance("alice"), std::nullopt);
}

TEST(Ledger, SetBalanceOnNewAddressWorks) {
    Ledger ledger;
    ledger.set_balance("alice", 1000);
    EXPECT_EQ(ledger.get_balance("alice"), std::optional<uint64_t>(1000));
}

TEST(Ledger, SetBalanceOverwritesExistingBalance) {
    Ledger ledger;
    ledger.set_balance("alice", 1000);
    ledger.set_balance("alice", 500);
    EXPECT_EQ(ledger.get_balance("alice"), std::optional<uint64_t>(500));
}

TEST(Ledger, SetBalanceToZeroIsDistinctFromUnknownAddress) {
    Ledger ledger;
    ledger.set_balance("alice", 0);
    EXPECT_EQ(ledger.get_balance("alice"), std::optional<uint64_t>(0));
    EXPECT_NE(ledger.get_balance("alice"), std::nullopt);
}

TEST(Ledger, MultipleAddressesAreIndependent) {
    Ledger ledger;
    ledger.set_balance("alice", 1000);
    ledger.set_balance("bob", 50);

    EXPECT_EQ(ledger.get_balance("alice"), std::optional<uint64_t>(1000));
    EXPECT_EQ(ledger.get_balance("bob"), std::optional<uint64_t>(50));
    EXPECT_EQ(ledger.get_balance("charlie"), std::nullopt);
}

TEST(Ledger, ApplyTransactionMovesFundsCorrectly) {
    Ledger ledger;
    ledger.set_balance("alice", 1000);
    ledger.set_balance("bob", 0);

    Transaction tx("alice", "bob", 300);
    EXPECT_TRUE(ledger.apply_transaction(tx));

    EXPECT_EQ(ledger.get_balance("alice"), std::optional<uint64_t>(700));
    EXPECT_EQ(ledger.get_balance("bob"), std::optional<uint64_t>(300));
}

TEST(Ledger, ApplyTransactionToBrandNewRecipientWorks) {
    Ledger ledger;
    ledger.set_balance("alice", 1000);

    Transaction tx("alice", "bob", 300);
    EXPECT_TRUE(ledger.apply_transaction(tx));

    EXPECT_EQ(ledger.get_balance("bob"), std::optional<uint64_t>(300));
}

TEST(Ledger, ApplyTransactionForExactBalanceLeavesSenderAtZero) {
    Ledger ledger;
    ledger.set_balance("alice", 500);

    Transaction tx("alice", "bob", 500);
    EXPECT_TRUE(ledger.apply_transaction(tx));

    EXPECT_EQ(ledger.get_balance("alice"), std::optional<uint64_t>(0));
    EXPECT_EQ(ledger.get_balance("bob"), std::optional<uint64_t>(500));
}

TEST(Ledger, ApplyTransactionOfZeroAmountSucceedsAndChangesNothing) {
    Ledger ledger;
    ledger.set_balance("alice", 1000);
    ledger.set_balance("bob", 200);

    Transaction tx("alice", "bob", 0);
    EXPECT_TRUE(ledger.apply_transaction(tx));

    EXPECT_EQ(ledger.get_balance("alice"), std::optional<uint64_t>(1000));
    EXPECT_EQ(ledger.get_balance("bob"), std::optional<uint64_t>(200));
}

TEST(Ledger, ApplyTransactionFailsWhenSenderUnknown) {
    Ledger ledger;
    ledger.set_balance("bob", 100);

    Transaction tx("alice", "bob", 50);
    EXPECT_FALSE(ledger.apply_transaction(tx));

    EXPECT_EQ(ledger.get_balance("alice"), std::nullopt);
    EXPECT_EQ(ledger.get_balance("bob"), std::optional<uint64_t>(100));
}

TEST(Ledger, ApplyTransactionFailsWhenBalanceInsufficient) {
    Ledger ledger;
    ledger.set_balance("alice", 100);
    ledger.set_balance("bob", 0);

    Transaction tx("alice", "bob", 101);
    EXPECT_FALSE(ledger.apply_transaction(tx));

    EXPECT_EQ(ledger.get_balance("alice"), std::optional<uint64_t>(100));
    EXPECT_EQ(ledger.get_balance("bob"), std::optional<uint64_t>(0));
}

TEST(Ledger, ApplyTransactionFailsWhenSenderHasExactlyOneLessThanNeeded) {
    Ledger ledger;
    ledger.set_balance("alice", 499);

    Transaction tx("alice", "bob", 500);
    EXPECT_FALSE(ledger.apply_transaction(tx));
    EXPECT_EQ(ledger.get_balance("alice"), std::optional<uint64_t>(499));
}

TEST(Ledger, FailedTransactionDoesNotCreateRecipientEntry) {
    Ledger ledger;
    ledger.set_balance("alice", 10);

    Transaction tx("alice", "bob", 999);
    EXPECT_FALSE(ledger.apply_transaction(tx));

    EXPECT_EQ(ledger.get_balance("bob"), std::nullopt);
}

TEST(Ledger, SelfTransferLeavesBalanceUnchanged) {
    Ledger ledger;
    ledger.set_balance("alice", 1000);

    Transaction tx("alice", "alice", 300);
    EXPECT_TRUE(ledger.apply_transaction(tx));

    EXPECT_EQ(ledger.get_balance("alice"), std::optional<uint64_t>(1000));
}

TEST(Ledger, SelfTransferOfEntireBalanceSucceeds) {
    Ledger ledger;
    ledger.set_balance("alice", 500);

    Transaction tx("alice", "alice", 500);
    EXPECT_TRUE(ledger.apply_transaction(tx));

    EXPECT_EQ(ledger.get_balance("alice"), std::optional<uint64_t>(500));
}

TEST(Ledger, SequentialTransactionsAccumulateCorrectly) {
    Ledger ledger;
    ledger.set_balance("alice", 1000);
    ledger.set_balance("bob", 0);
    ledger.set_balance("charlie", 0);

    EXPECT_TRUE(ledger.apply_transaction(Transaction("alice", "bob", 300)));
    EXPECT_TRUE(ledger.apply_transaction(Transaction("bob", "charlie", 100)));
    EXPECT_TRUE(ledger.apply_transaction(Transaction("alice", "charlie", 200)));

    EXPECT_EQ(ledger.get_balance("alice"), std::optional<uint64_t>(500));
    EXPECT_EQ(ledger.get_balance("bob"), std::optional<uint64_t>(200));
    EXPECT_EQ(ledger.get_balance("charlie"), std::optional<uint64_t>(300));
}

TEST(Ledger, RejectedTransactionInSequenceDoesNotDisruptLaterOnes) {
    Ledger ledger;
    ledger.set_balance("alice", 100);
    ledger.set_balance("bob", 0);

    EXPECT_TRUE(ledger.apply_transaction(Transaction("alice", "bob", 50)));
    EXPECT_FALSE(ledger.apply_transaction(Transaction("alice", "bob", 999)));
    EXPECT_TRUE(ledger.apply_transaction(Transaction("alice", "bob", 50)));

    EXPECT_EQ(ledger.get_balance("alice"), std::optional<uint64_t>(0));
    EXPECT_EQ(ledger.get_balance("bob"), std::optional<uint64_t>(100));
}

TEST(Ledger, ManySequentialTransfersLeaveConsistentTotalSupply) {
    Ledger ledger;
    ledger.set_balance("alice", 10000);
    ledger.set_balance("bob", 0);
    ledger.set_balance("charlie", 0);

    constexpr uint64_t kInitialTotal = 10000;

    for (int i = 0; i < 100; ++i) {
        const char* from = (i % 3 == 0) ? "alice" : (i % 3 == 1) ? "bob" : "charlie";
        const char* to = (i % 3 == 0) ? "bob" : (i % 3 == 1) ? "charlie" : "alice";
        ledger.apply_transaction(Transaction(from, to, 10));
    }

    uint64_t total = ledger.get_balance("alice").value_or(0) +
                      ledger.get_balance("bob").value_or(0) +
                      ledger.get_balance("charlie").value_or(0);

    EXPECT_EQ(total, kInitialTotal);
}
