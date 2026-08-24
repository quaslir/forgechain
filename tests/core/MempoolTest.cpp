#include "core/Mempool.hpp"
#include "core/Transaction.hpp"
#include "crypto/Address.hpp"
#include "crypto/Keys.hpp"
#include "crypto/Signature.hpp"

#include <gtest/gtest.h>
#include <cstdint>
#include <vector>
#include <cstddef>
#include <string>
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

Transaction make_signed_tx(const Wallet& sender, const str& recipient, uint64_t amount,
                            bytes* out_signature = nullptr) {
    Transaction tx(sender.address, recipient, amount, sender.keys.public_key);
    bytes signature = sign(tx.serialize_for_signing(), sender.keys.private_key);
    tx.signature_ = signature;
    if (out_signature) *out_signature = signature;
    return tx;
}

}  // namespace

TEST(Mempool, AcceptsValidSignedTransaction) {
    Mempool mempool;
    Wallet alice = make_wallet();
    Transaction tx = make_signed_tx(alice, "bob-address", 100);

    EXPECT_TRUE(mempool.add_transaction(tx, alice.keys.public_key));
    EXPECT_EQ(mempool.size(), 1u);
}

TEST(Mempool, RejectsTransactionWithInvalidSignature) {
    Mempool mempool;
    Wallet alice = make_wallet();
    Wallet mallory = make_wallet();

    Transaction tx(alice.address, "bob-address", 100, alice.keys.public_key);
    tx.signature_ = sign(tx.serialize_for_signing(), mallory.keys.private_key);

    EXPECT_FALSE(mempool.add_transaction(tx, alice.keys.public_key));
    EXPECT_EQ(mempool.size(), 0u);
}

TEST(Mempool, RejectsTransactionWhenPublicKeyDoesNotMatchClaimedSender) {
    Mempool mempool;
    Wallet alice = make_wallet();
    Wallet mallory = make_wallet();

    Transaction tx(alice.address, "bob-address", 100, mallory.keys.public_key);
    tx.signature_ = sign(tx.serialize_for_signing(), mallory.keys.private_key);

    EXPECT_TRUE(verify(tx.serialize_for_signing(), tx.signature_, mallory.keys.public_key));
    EXPECT_FALSE(mempool.add_transaction(tx, mallory.keys.public_key));
    EXPECT_EQ(mempool.size(), 0u);
}

TEST(Mempool, RejectsTransactionWithTamperedAmount) {
    Mempool mempool;
    Wallet alice = make_wallet();

    Transaction original(alice.address, "bob-address", 100, alice.keys.public_key);
    bytes signature = sign(original.serialize_for_signing(), alice.keys.private_key);

    Transaction tampered(alice.address, "bob-address", 999999, alice.keys.public_key);
    tampered.signature_ = signature;

    EXPECT_FALSE(mempool.add_transaction(tampered, alice.keys.public_key));
    EXPECT_EQ(mempool.size(), 0u);
}

TEST(Mempool, RejectsTransactionWithEmptySignature) {
    Mempool mempool;
    Wallet alice = make_wallet();

    Transaction tx(alice.address, "bob-address", 100, alice.keys.public_key);

    EXPECT_FALSE(mempool.add_transaction(tx, alice.keys.public_key));
}

TEST(Mempool, AcceptsMultipleDistinctTransactionsFromSameSender) {
    Mempool mempool;
    Wallet alice = make_wallet();

    Transaction tx1 = make_signed_tx(alice, "bob-address", 100);
    Transaction tx2 = make_signed_tx(alice, "charlie-address", 50);

    EXPECT_TRUE(mempool.add_transaction(tx1, alice.keys.public_key));
    EXPECT_TRUE(mempool.add_transaction(tx2, alice.keys.public_key));
    EXPECT_EQ(mempool.size(), 2u);
}

TEST(Mempool, AcceptsTransactionsFromMultipleDistinctSenders) {
    Mempool mempool;
    Wallet alice = make_wallet();
    Wallet bob = make_wallet();

    Transaction tx1 = make_signed_tx(alice, "charlie-address", 100);
    Transaction tx2 = make_signed_tx(bob, "charlie-address", 50);

    EXPECT_TRUE(mempool.add_transaction(tx1, alice.keys.public_key));
    EXPECT_TRUE(mempool.add_transaction(tx2, bob.keys.public_key));
    EXPECT_EQ(mempool.size(), 2u);
}

TEST(Mempool, StartsEmpty) {
    Mempool mempool;
    EXPECT_TRUE(mempool.empty());
    EXPECT_EQ(mempool.size(), 0u);
}

TEST(Mempool, NotEmptyAfterAcceptedTransaction) {
    Mempool mempool;
    Wallet alice = make_wallet();
    mempool.add_transaction(make_signed_tx(alice, "bob-address", 10), alice.keys.public_key);

    EXPECT_FALSE(mempool.empty());
}

TEST(Mempool, RemainsEmptyAfterRejectedTransaction) {
    Mempool mempool;
    Wallet alice = make_wallet();
    Wallet mallory = make_wallet();

    Transaction tx(alice.address, "bob-address", 10, alice.keys.public_key);
    tx.signature_ = sign(tx.serialize_for_signing(), mallory.keys.private_key);
    mempool.add_transaction(tx, alice.keys.public_key);

    EXPECT_TRUE(mempool.empty());
}

TEST(Mempool, RemoveTransactionDecreasesSize) {
    Mempool mempool;
    Wallet alice = make_wallet();
    Transaction tx = make_signed_tx(alice, "bob-address", 100);
    mempool.add_transaction(tx, alice.keys.public_key);

    mempool.remove_transaction(tx);
    EXPECT_EQ(mempool.size(), 0u);
    EXPECT_TRUE(mempool.empty());
}

TEST(Mempool, RemoveOnlyRemovesTheMatchingTransaction) {
    Mempool mempool;
    Wallet alice = make_wallet();
    Transaction tx1 = make_signed_tx(alice, "bob-address", 100);
    Transaction tx2 = make_signed_tx(alice, "charlie-address", 50);
    mempool.add_transaction(tx1, alice.keys.public_key);
    mempool.add_transaction(tx2, alice.keys.public_key);

    mempool.remove_transaction(tx1);

    EXPECT_EQ(mempool.size(), 1u);
    auto remaining = mempool.get_transactions_for_block(10);
    ASSERT_EQ(remaining.size(), 1u);
    EXPECT_EQ(remaining[0].recipient_, "charlie-address");
}

TEST(Mempool, RemovingNonExistentTransactionIsANoOp) {
    Mempool mempool;
    Wallet alice = make_wallet();
    Transaction tx1 = make_signed_tx(alice, "bob-address", 100);
    mempool.add_transaction(tx1, alice.keys.public_key);

    Transaction neverAdded = make_signed_tx(alice, "someone-else", 999);
    EXPECT_NO_THROW(mempool.remove_transaction(neverAdded));
    EXPECT_EQ(mempool.size(), 1u);
}

TEST(Mempool, RemoveThenReAddSameTransactionWorks) {
    Mempool mempool;
    Wallet alice = make_wallet();
    Transaction tx = make_signed_tx(alice, "bob-address", 100);

    mempool.add_transaction(tx, alice.keys.public_key);
    mempool.remove_transaction(tx);
    EXPECT_TRUE(mempool.empty());

    EXPECT_TRUE(mempool.add_transaction(tx, alice.keys.public_key));
    EXPECT_EQ(mempool.size(), 1u);
}

TEST(Mempool, GetTransactionsForBlockReturnsAllWhenLimitExceedsSize) {
    Mempool mempool;
    Wallet alice = make_wallet();
    mempool.add_transaction(make_signed_tx(alice, "bob-address", 10), alice.keys.public_key);
    mempool.add_transaction(make_signed_tx(alice, "charlie-address", 20), alice.keys.public_key);

    auto result = mempool.get_transactions_for_block(100);
    EXPECT_EQ(result.size(), 2u);
}

TEST(Mempool, GetTransactionsForBlockRespectsLimit) {
    Mempool mempool;
    Wallet alice = make_wallet();
    for (int i = 0; i < 5; ++i) {
        mempool.add_transaction(
            make_signed_tx(alice, "recipient-" + std::to_string(i), 10),
            alice.keys.public_key);
    }

    auto result = mempool.get_transactions_for_block(3);
    EXPECT_EQ(result.size(), 3u);
}

TEST(Mempool, GetTransactionsForBlockReturnsEarliestAddedFirst) {
    Mempool mempool;
    Wallet alice = make_wallet();
    Transaction first = make_signed_tx(alice, "first-recipient", 10);
    Transaction second = make_signed_tx(alice, "second-recipient", 20);
    Transaction third = make_signed_tx(alice, "third-recipient", 30);

    mempool.add_transaction(first, alice.keys.public_key);
    mempool.add_transaction(second, alice.keys.public_key);
    mempool.add_transaction(third, alice.keys.public_key);

    auto result = mempool.get_transactions_for_block(2);
    ASSERT_EQ(result.size(), 2u);
    EXPECT_EQ(result[0].recipient_, "first-recipient");
    EXPECT_EQ(result[1].recipient_, "second-recipient");
}

TEST(Mempool, GetTransactionsForBlockOnEmptyMempoolReturnsEmpty) {
    Mempool mempool;
    auto result = mempool.get_transactions_for_block(10);
    EXPECT_TRUE(result.empty());
}

TEST(Mempool, GetTransactionsForBlockWithZeroLimitReturnsEmpty) {
    Mempool mempool;
    Wallet alice = make_wallet();
    mempool.add_transaction(make_signed_tx(alice, "bob-address", 10), alice.keys.public_key);

    auto result = mempool.get_transactions_for_block(0);
    EXPECT_TRUE(result.empty());
}

TEST(Mempool, GetTransactionsForBlockDoesNotModifyMempool) {
    Mempool mempool;
    Wallet alice = make_wallet();
    mempool.add_transaction(make_signed_tx(alice, "bob-address", 10), alice.keys.public_key);
    mempool.add_transaction(make_signed_tx(alice, "charlie-address", 20), alice.keys.public_key);

    auto result = mempool.get_transactions_for_block(1);
    EXPECT_EQ(result.size(), 1u);
    EXPECT_EQ(mempool.size(), 2u);

    auto result2 = mempool.get_transactions_for_block(1);
    EXPECT_EQ(result2.size(), 1u);
    EXPECT_EQ(result[0].recipient_, result2[0].recipient_);
}

TEST(Mempool, SelectThenRemoveWorkflowLeavesOnlyUnselectedTransactions) {
    Mempool mempool;
    Wallet alice = make_wallet();
    for (int i = 0; i < 5; ++i) {
        mempool.add_transaction(
            make_signed_tx(alice, "recipient-" + std::to_string(i), 10),
            alice.keys.public_key);
    }

    auto selected = mempool.get_transactions_for_block(3);
    ASSERT_EQ(selected.size(), 3u);

    for (const auto& tx : selected) {
        mempool.remove_transaction(tx);
    }

    EXPECT_EQ(mempool.size(), 2u);
    auto remaining = mempool.get_transactions_for_block(10);
    EXPECT_EQ(remaining[0].recipient_, "recipient-3");
    EXPECT_EQ(remaining[1].recipient_, "recipient-4");
}

TEST(Mempool, ManyWalletsManyTransactionsAllAcceptedCorrectly) {
    constexpr int kWalletCount = 10;
    std::vector<Wallet> wallets;
    for (int i = 0; i < kWalletCount; ++i) {
        wallets.push_back(make_wallet());
    }

    Mempool mempool;
    for (int i = 0; i < kWalletCount; ++i) {
        Transaction tx = make_signed_tx(wallets[static_cast<size_t>(i)], "shared-recipient", 10);
        EXPECT_TRUE(mempool.add_transaction(tx, wallets[static_cast<size_t>(i)].keys.public_key))
            << "wallet " << i << " transaction was rejected";
    }

    EXPECT_EQ(mempool.size(), static_cast<size_t>(kWalletCount));
}

TEST(Mempool, RejectsCoinbaseMarkedTransaction) {
    Mempool mempool;
    Wallet alice = make_wallet();

    Transaction tx(kCoinbaseSender, alice.address, 50, alice.keys.public_key);
    tx.signature_ = sign(tx.serialize_for_signing(), alice.keys.private_key);

    EXPECT_FALSE(mempool.add_transaction(tx, alice.keys.public_key));
    EXPECT_EQ(mempool.size(), 0u);
}

TEST(Mempool, RejectsCoinbaseMarkedTransactionEvenWithEmptyKey) {
    Mempool mempool;

    Transaction tx(kCoinbaseSender, "alice-address", 50, {});

    EXPECT_FALSE(mempool.add_transaction(tx, {}));
    EXPECT_EQ(mempool.size(), 0u);
}
