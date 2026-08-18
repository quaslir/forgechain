#include "consensus/ProofOfWork.hpp"
#include "core/Block.hpp"
#include "core/Blockchain.hpp"
#include "core/Ledger.hpp"
#include "core/Mempool.hpp"
#include "core/Transaction.hpp"
#include "crypto/Address.hpp"
#include "crypto/Keys.hpp"
#include "crypto/Signature.hpp"
#include <optional>
#include <cstdint>
#include <cstddef>
#include <gtest/gtest.h>
#include <vector>
using namespace forgechain::core;
using namespace forgechain::consensus;
using namespace forgechain::crypto;

namespace {

constexpr uint32_t kTestDifficulty = 8;

struct Wallet {
    KeyPair keys;
    str address;
};

Wallet make_wallet() {
    KeyPair kp = generate_keypair();
    return Wallet{kp, derive_address(kp.public_key)};
}

Transaction make_signed_tx(const Wallet& sender, const str& recipient, uint64_t amount) {
    Transaction tx(sender.address, recipient, amount, sender.keys.public_key);
    tx.signature_ = sign(tx.serialize_for_signing(), sender.keys.private_key);
    return tx;
}

}  // namespace

TEST(FullBlockchainLifecycle, WalletToMinedBlockToLedgerEndToEnd) {
    Wallet alice = make_wallet();
    Wallet bob = make_wallet();

    Blockchain chain;
    Ledger ledger;
    ledger.set_balance(alice.address, 1000);

    Mempool mempool;
    Transaction tx = make_signed_tx(alice, bob.address, 300);
    ASSERT_TRUE(mempool.add_transaction(tx, alice.keys.public_key));

    auto txsForBlock = mempool.get_transactions_for_block(10);
    ASSERT_EQ(txsForBlock.size(), 1u);

    Block mined = mine_block(1, chain.latest().hash_, 1700000000, kTestDifficulty, txsForBlock);
    EXPECT_TRUE(meets_target(mined.hash_, kTestDifficulty));
    ASSERT_EQ(mined.transactions_.size(), 1u);

    for (const auto& minedTx : mined.transactions_) {
        EXPECT_TRUE(ledger.apply_transaction(minedTx));
        mempool.remove_transaction(minedTx);
    }
    chain.add_block(mined);

    EXPECT_EQ(chain.size(), 2u);
    EXPECT_TRUE(mempool.empty());
    EXPECT_TRUE(chain.is_valid());
    EXPECT_EQ(ledger.get_balance(alice.address), std::optional<uint64_t>(700));
    EXPECT_EQ(ledger.get_balance(bob.address), std::optional<uint64_t>(300));
}

TEST(FullBlockchainLifecycle, MultipleTransactionsInOneMinedBlock) {
    Wallet alice = make_wallet();
    Wallet bob = make_wallet();
    Wallet charlie = make_wallet();

    Blockchain chain;
    Ledger ledger;
    ledger.set_balance(alice.address, 1000);

    Mempool mempool;
    ASSERT_TRUE(mempool.add_transaction(make_signed_tx(alice, bob.address, 200), alice.keys.public_key));
    ASSERT_TRUE(mempool.add_transaction(make_signed_tx(alice, charlie.address, 100), alice.keys.public_key));

    auto txsForBlock = mempool.get_transactions_for_block(10);
    ASSERT_EQ(txsForBlock.size(), 2u);

    Block mined = mine_block(1, chain.latest().hash_, 1700000000, kTestDifficulty, txsForBlock);

    for (const auto& tx : mined.transactions_) {
        ASSERT_TRUE(ledger.apply_transaction(tx));
        mempool.remove_transaction(tx);
    }
    chain.add_block(mined);

    EXPECT_TRUE(mempool.empty());
    EXPECT_TRUE(chain.is_valid());
    EXPECT_EQ(ledger.get_balance(alice.address), std::optional<uint64_t>(700));
    EXPECT_EQ(ledger.get_balance(bob.address), std::optional<uint64_t>(200));
    EXPECT_EQ(ledger.get_balance(charlie.address), std::optional<uint64_t>(100));
}

TEST(FullBlockchainLifecycle, TwoBlocksMinedSequentiallyDrainMempoolCorrectly) {
    Wallet alice = make_wallet();
    Wallet bob = make_wallet();
    Wallet charlie = make_wallet();

    Blockchain chain;
    Ledger ledger;
    ledger.set_balance(alice.address, 1000);
    ledger.set_balance(bob.address, 0);

    Mempool mempool;
    ASSERT_TRUE(mempool.add_transaction(make_signed_tx(alice, bob.address, 300), alice.keys.public_key));
    ASSERT_TRUE(mempool.add_transaction(make_signed_tx(alice, charlie.address, 150), alice.keys.public_key));
    ASSERT_TRUE(mempool.add_transaction(make_signed_tx(bob, charlie.address, 50), bob.keys.public_key));

    ASSERT_EQ(mempool.size(), 3u);

    auto block1Txs = mempool.get_transactions_for_block(2);
    ASSERT_EQ(block1Txs.size(), 2u);
    Block block1 = mine_block(1, chain.latest().hash_, 1700000000, kTestDifficulty, block1Txs);
    for (const auto& tx : block1.transactions_) {
        ASSERT_TRUE(ledger.apply_transaction(tx));
        mempool.remove_transaction(tx);
    }
    chain.add_block(block1);

    EXPECT_EQ(mempool.size(), 1u);

    auto block2Txs = mempool.get_transactions_for_block(10);
    ASSERT_EQ(block2Txs.size(), 1u);
    Block block2 = mine_block(1, chain.latest().hash_, 1700000001, kTestDifficulty, block2Txs);
    for (const auto& tx : block2.transactions_) {
        ASSERT_TRUE(ledger.apply_transaction(tx));
        mempool.remove_transaction(tx);
    }
    chain.add_block(block2);

    EXPECT_TRUE(mempool.empty());
    EXPECT_EQ(chain.size(), 3u);
    EXPECT_TRUE(chain.is_valid());

    EXPECT_EQ(ledger.get_balance(alice.address), std::optional<uint64_t>(550));
    EXPECT_EQ(ledger.get_balance(bob.address), std::optional<uint64_t>(250));
    EXPECT_EQ(ledger.get_balance(charlie.address), std::optional<uint64_t>(200));
}

TEST(FullBlockchainLifecycle, TotalSupplyConservedAcrossMinedBlocks) {
    Wallet alice = make_wallet();
    Wallet bob = make_wallet();
    Wallet charlie = make_wallet();

    Blockchain chain;
    Ledger ledger;
    constexpr uint64_t kInitialSupply = 10000;
    ledger.set_balance(alice.address, kInitialSupply);

    Mempool mempool;
    ASSERT_TRUE(mempool.add_transaction(make_signed_tx(alice, bob.address, 4000), alice.keys.public_key));
    ASSERT_TRUE(mempool.add_transaction(make_signed_tx(bob, charlie.address, 1500), bob.keys.public_key));
    ASSERT_TRUE(mempool.add_transaction(make_signed_tx(charlie, alice.address, 500), charlie.keys.public_key));

    for (int round = 0; round < 3 && !mempool.empty(); ++round) {
        auto txs = mempool.get_transactions_for_block(1);
        if (txs.empty()) break;
        Block mined = mine_block(1, chain.latest().hash_,
                                  1700000000 + static_cast<uint64_t>(round),
                                  kTestDifficulty, txs);
        for (const auto& tx : mined.transactions_) {
            if (ledger.apply_transaction(tx)) {
                mempool.remove_transaction(tx);
            }
        }
        chain.add_block(mined);
    }

    EXPECT_TRUE(chain.is_valid());

    uint64_t total = ledger.get_balance(alice.address).value_or(0) +
                      ledger.get_balance(bob.address).value_or(0) +
                      ledger.get_balance(charlie.address).value_or(0);
    EXPECT_EQ(total, kInitialSupply);
}

TEST(FullBlockchainLifecycle, TamperedMinedTransactionFailsBlockIntegrityCheck) {
    Wallet alice = make_wallet();
    Wallet bob = make_wallet();

    Blockchain chain;
    Mempool mempool;
    ASSERT_TRUE(mempool.add_transaction(make_signed_tx(alice, bob.address, 300), alice.keys.public_key));

    auto txs = mempool.get_transactions_for_block(10);
    Block mined = mine_block(1, chain.latest().hash_, 1700000000, kTestDifficulty, txs);
    chain.add_block(mined);

    ASSERT_TRUE(chain.is_valid());

    Block tamperedCopy = chain.at(1);
    ASSERT_FALSE(tamperedCopy.transactions_.empty());
    tamperedCopy.transactions_[0].amount_ = 999999;

    EXPECT_NE(tamperedCopy.compute_hash(), tamperedCopy.hash_);

    EXPECT_FALSE(verify(tamperedCopy.transactions_[0].serialize_for_signing(),
                         tamperedCopy.transactions_[0].signature_,
                         alice.keys.public_key));
}

TEST(FullBlockchainLifecycle, ChainRemainsValidWhenTamperedBlockIsNotActuallyInserted) {
    Wallet alice = make_wallet();
    Wallet bob = make_wallet();

    Blockchain chain;
    Mempool mempool;
    ASSERT_TRUE(mempool.add_transaction(make_signed_tx(alice, bob.address, 300), alice.keys.public_key));

    auto txs = mempool.get_transactions_for_block(10);
    Block mined = mine_block(1, chain.latest().hash_, 1700000000, kTestDifficulty, txs);
    chain.add_block(mined);

    Block tamperedCopy = chain.at(1);
    tamperedCopy.transactions_[0].amount_ = 999999;

    EXPECT_TRUE(chain.is_valid());
    EXPECT_EQ(chain.at(1).transactions_[0].amount_, 300u);
}

TEST(FullBlockchainLifecycle, ForgedTransactionNeverReachesAMinedBlock) {
    Wallet alice = make_wallet();
    Wallet bob = make_wallet();
    Wallet mallory = make_wallet();

    Mempool mempool;

    Transaction forged(alice.address, mallory.address, 500, mallory.keys.public_key);
    forged.signature_ = sign(forged.serialize_for_signing(), mallory.keys.private_key);

    EXPECT_FALSE(mempool.add_transaction(forged, mallory.keys.public_key));
    EXPECT_TRUE(mempool.empty());

    EXPECT_TRUE(mempool.add_transaction(make_signed_tx(alice, bob.address, 100), alice.keys.public_key));
    EXPECT_EQ(mempool.size(), 1u);
}

TEST(FullBlockchainLifecycle, MultipleWalletsMultipleRoundsStayConsistent) {
    constexpr int kWalletCount = 5;
    std::vector<Wallet> wallets;
    for (int i = 0; i < kWalletCount; ++i) {
        wallets.push_back(make_wallet());
    }

    Blockchain chain;
    Ledger ledger;
    Mempool mempool;

    for (auto& w : wallets) {
        ledger.set_balance(w.address, 100);
    }

    for (int i = 0; i < kWalletCount; ++i) {
        int next = (i + 1) % kWalletCount;
        Transaction tx = make_signed_tx(wallets[static_cast<size_t>(i)],
                                         wallets[static_cast<size_t>(next)].address, 10);
        ASSERT_TRUE(mempool.add_transaction(tx, wallets[static_cast<size_t>(i)].keys.public_key));
    }

    auto txs = mempool.get_transactions_for_block(static_cast<size_t>(kWalletCount));
    ASSERT_EQ(txs.size(), static_cast<size_t>(kWalletCount));

    Block mined = mine_block(1, chain.latest().hash_, 1700000000, kTestDifficulty, txs);
    for (const auto& tx : mined.transactions_) {
        EXPECT_TRUE(ledger.apply_transaction(tx));
        mempool.remove_transaction(tx);
    }
    chain.add_block(mined);

    EXPECT_TRUE(chain.is_valid());
    EXPECT_TRUE(mempool.empty());

    for (const auto& w : wallets) {
        EXPECT_EQ(ledger.get_balance(w.address), std::optional<uint64_t>(100));
    }
}
