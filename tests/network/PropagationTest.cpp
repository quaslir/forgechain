#include "network/Node.hpp"
#include "network/Handshake.hpp"
#include "network/Message.hpp"
#include "network/TcpSocket.hpp"
#include "core/Blockchain.hpp"
#include "core/Mempool.hpp"
#include "core/OrphanPool.hpp"
#include "core/Ledger.hpp"
#include "core/Block.hpp"
#include "core/Transaction.hpp"
#include "consensus/ProofOfWork.hpp"
#include "crypto/CommonTypes.hpp"
#include "crypto/Keys.hpp"
#include "crypto/Address.hpp"
#include "crypto/Signature.hpp"

#include <gtest/gtest.h>

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

using namespace forgechain::network;
using namespace forgechain::core;
using namespace forgechain::consensus;
using namespace forgechain::crypto;

namespace {

uint16_t next_test_port() {
    static auto port = static_cast<uint16_t>(34000 + (getpid() % 1000) * 20);
    return port++;
}

uint64_t next_test_node_id() {
    static std::atomic<uint64_t> counter{1};
    return counter.fetch_add(1);
}

VersionInfo make_version(uint16_t listen_port, uint64_t height = 0) {
    return VersionInfo{.protocol_version = 1, .chain_height = height,
                       .timestamp = 0, .listen_port = listen_port,
                       .node_id = next_test_node_id()};
}

template <typename Predicate>
bool WaitUntil(Predicate predicate, std::chrono::milliseconds timeout) {
    auto start = std::chrono::steady_clock::now();
    while (!predicate()) {
        if (std::chrono::steady_clock::now() - start > timeout) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return true;
}

struct Wallet {
    KeyPair keys;
    str address;
};

Wallet make_wallet() {
    KeyPair kp = generate_keypair();
    return Wallet{kp, derive_address(kp.public_key)};
}

Transaction make_signed_tx(const Wallet& sender, const str& recipient, uint64_t amount, uint64_t fee = 0) {
    Transaction tx(sender.address, recipient, amount, sender.keys.public_key, fee);
    tx.signature_ = sign(tx.serialize_for_signing(), sender.keys.private_key);
    return tx;
}

class RawPeer {
public:
    bool connect(uint16_t port, const VersionInfo& info) {
        socket_ = std::make_unique<TcpSocket>(connect_to("127.0.0.1", port));
        if (!socket_->is_valid()) return false;
        auto remote = perform_handshake(socket_->fd(), info);
        return remote.has_value();
    }

    bool send(MessageType type, const bytes& payload) {
        Message msg{.type = type, .payload = payload};
        return send_message(socket_->fd(), msg);
    }

private:
    std::unique_ptr<TcpSocket> socket_;
};

}  // namespace

TEST(Propagation, ValidBlockFromOnePeerReachesSecondPeer) {
    uint16_t port_a = next_test_port();
    uint16_t port_b = next_test_port();

    Blockchain chain_a;
    Mempool mempool_a;
    OrphanPool orphan_pool_a;
    Ledger ledger_a;
    Node node_a(port_a, make_version(port_a), chain_a, mempool_a, orphan_pool_a, ledger_a);
    ASSERT_TRUE(node_a.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    Blockchain chain_b;
    Mempool mempool_b;
    OrphanPool orphan_pool_b;
    Ledger ledger_b;
    Node node_b(port_b, make_version(port_b), chain_b, mempool_b, orphan_pool_b, ledger_b);
    ASSERT_TRUE(node_b.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_TRUE(node_b.connect_to_peer("127.0.0.1", port_a));
    ASSERT_TRUE(WaitUntil([&] { return node_a.peer_count() >= 1; }, std::chrono::seconds(1)));

    RawPeer source;
    ASSERT_TRUE(source.connect(port_a, make_version(port_a)));
    ASSERT_TRUE(WaitUntil([&] { return node_a.peer_count() >= 2; }, std::chrono::seconds(1)));

    constexpr uint32_t kTestDifficulty = 8;
    Block mined = mine_block(1, chain_a.latest().hash_, 1700000000, kTestDifficulty, {});
    ASSERT_TRUE(source.send(MessageType::BLOCK, mined.serialize()));

    ASSERT_TRUE(WaitUntil([&] { return chain_a.has_block(mined.hash_); }, std::chrono::seconds(2)))
        << "Node A never accepted the block into its own chain";

    ASSERT_TRUE(WaitUntil([&] { return chain_b.has_block(mined.hash_); }, std::chrono::seconds(2)))
        << "Node B never received the block relayed by Node A";
}

TEST(Propagation, ValidTransactionFromOnePeerReachesSecondPeer) {
    uint16_t port_a = next_test_port();
    uint16_t port_b = next_test_port();

    Blockchain chain_a;
    Mempool mempool_a;
    OrphanPool orphan_pool_a;
    Ledger ledger_a;
    Node node_a(port_a, make_version(port_a), chain_a, mempool_a, orphan_pool_a, ledger_a);
    ASSERT_TRUE(node_a.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    Blockchain chain_b;
    Mempool mempool_b;
    OrphanPool orphan_pool_b;
    Ledger ledger_b;
    Node node_b(port_b, make_version(port_b), chain_b, mempool_b, orphan_pool_b, ledger_b);
    ASSERT_TRUE(node_b.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_TRUE(node_b.connect_to_peer("127.0.0.1", port_a));
    ASSERT_TRUE(WaitUntil([&] { return node_a.peer_count() >= 1; }, std::chrono::seconds(1)));

    RawPeer source;
    ASSERT_TRUE(source.connect(port_a, make_version(port_a)));
    ASSERT_TRUE(WaitUntil([&] { return node_a.peer_count() >= 2; }, std::chrono::seconds(1)));

    Wallet alice = make_wallet();
    Transaction tx = make_signed_tx(alice, "bob-address", 100);
    auto tx_hash = tx.compute_hash();

    ASSERT_TRUE(source.send(MessageType::TX, tx.serialize()));

    ASSERT_TRUE(WaitUntil([&] { return mempool_a.has_transaction(tx_hash); }, std::chrono::seconds(2)))
        << "Node A never accepted the transaction into its own mempool";

    ASSERT_TRUE(WaitUntil([&] { return mempool_b.has_transaction(tx_hash); }, std::chrono::seconds(2)))
        << "Node B never received the transaction relayed by Node A";
}

TEST(Propagation, ForgedTransactionIsNeitherStoredNorRelayed) {
    uint16_t port_a = next_test_port();
    uint16_t port_b = next_test_port();

    Blockchain chain_a;
    Mempool mempool_a;
    OrphanPool orphan_pool_a;
    Ledger ledger_a;
    Node node_a(port_a, make_version(port_a), chain_a, mempool_a, orphan_pool_a, ledger_a);
    ASSERT_TRUE(node_a.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    Blockchain chain_b;
    Mempool mempool_b;
    OrphanPool orphan_pool_b;
    Ledger ledger_b;
    Node node_b(port_b, make_version(port_b), chain_b, mempool_b, orphan_pool_b, ledger_b);
    ASSERT_TRUE(node_b.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_TRUE(node_b.connect_to_peer("127.0.0.1", port_a));
    ASSERT_TRUE(WaitUntil([&] { return node_a.peer_count() >= 1; }, std::chrono::seconds(1)));

    RawPeer source;
    ASSERT_TRUE(source.connect(port_a, make_version(port_a)));
    ASSERT_TRUE(WaitUntil([&] { return node_a.peer_count() >= 2; }, std::chrono::seconds(1)));

    Wallet alice = make_wallet();
    Wallet mallory = make_wallet();

    Transaction forged(alice.address, "bob-address", 500, mallory.keys.public_key, 0);
    forged.signature_ = sign(forged.serialize_for_signing(), mallory.keys.private_key);
    auto forged_hash = forged.compute_hash();

    ASSERT_TRUE(source.send(MessageType::TX, forged.serialize()));

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_FALSE(mempool_a.has_transaction(forged_hash));
    EXPECT_FALSE(mempool_b.has_transaction(forged_hash));
}

TEST(Propagation, BlockWithWrongPrevHashIsNeitherStoredNorRelayed) {
    uint16_t port_a = next_test_port();
    uint16_t port_b = next_test_port();

    Blockchain chain_a;
    Mempool mempool_a;
    OrphanPool orphan_pool_a;
    Ledger ledger_a;
    Node node_a(port_a, make_version(port_a), chain_a, mempool_a, orphan_pool_a, ledger_a);
    ASSERT_TRUE(node_a.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    Blockchain chain_b;
    Mempool mempool_b;
    OrphanPool orphan_pool_b;
    Ledger ledger_b;
    Node node_b(port_b, make_version(port_b), chain_b, mempool_b, orphan_pool_b, ledger_b);
    ASSERT_TRUE(node_b.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_TRUE(node_b.connect_to_peer("127.0.0.1", port_a));
    ASSERT_TRUE(WaitUntil([&] { return node_a.peer_count() >= 1; }, std::chrono::seconds(1)));

    RawPeer source;
    ASSERT_TRUE(source.connect(port_a, make_version(port_a)));
    ASSERT_TRUE(WaitUntil([&] { return node_a.peer_count() >= 2; }, std::chrono::seconds(1)));

    constexpr uint32_t kTestDifficulty = 8;
    HashBytes wrong_parent{};
    wrong_parent[0] = 0xAB;
    wrong_parent[1] = 0xCD;
    Block orphan = mine_block(1, wrong_parent, 1700000000, kTestDifficulty, {});
    ASSERT_NE(orphan.prev_hash_, chain_a.latest().hash_);

    size_t height_before = chain_a.size();
    ASSERT_TRUE(source.send(MessageType::BLOCK, orphan.serialize()));

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(chain_a.size(), height_before)
        << "Node A accepted a block with a prev_hash_ that doesn't match its chain tip";
    EXPECT_FALSE(chain_b.has_block(orphan.hash_))
        << "Node B received a block that A should never have relayed";
}

TEST(Propagation, BlockWithTamperedHashIsNeitherStoredNorRelayed) {
    uint16_t port_a = next_test_port();
    uint16_t port_b = next_test_port();

    Blockchain chain_a;
    Mempool mempool_a;
    OrphanPool orphan_pool_a;
    Ledger ledger_a;
    Node node_a(port_a, make_version(port_a), chain_a, mempool_a, orphan_pool_a, ledger_a);
    ASSERT_TRUE(node_a.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    Blockchain chain_b;
    Mempool mempool_b;
    OrphanPool orphan_pool_b;
    Ledger ledger_b;
    Node node_b(port_b, make_version(port_b), chain_b, mempool_b, orphan_pool_b, ledger_b);
    ASSERT_TRUE(node_b.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_TRUE(node_b.connect_to_peer("127.0.0.1", port_a));
    ASSERT_TRUE(WaitUntil([&] { return node_a.peer_count() >= 1; }, std::chrono::seconds(1)));

    RawPeer source;
    ASSERT_TRUE(source.connect(port_a, make_version(port_a)));
    ASSERT_TRUE(WaitUntil([&] { return node_a.peer_count() >= 2; }, std::chrono::seconds(1)));

    constexpr uint32_t kTestDifficulty = 8;
    Block mined = mine_block(1, chain_a.latest().hash_, 1700000000, kTestDifficulty, {});

    Block tampered = mined;
    tampered.timestamp_ = mined.timestamp_ + 12345;
    ASSERT_NE(tampered.compute_hash(), tampered.hash_)
        << "test setup invariant broken: tampering did not change compute_hash()";

    size_t height_before = chain_a.size();
    ASSERT_TRUE(source.send(MessageType::BLOCK, tampered.serialize()));

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(chain_a.size(), height_before)
        << "Node A accepted a block whose hash_ doesn't match compute_hash()";
    EXPECT_FALSE(chain_b.has_block(tampered.hash_))
        << "Node B received a block that A should never have relayed";
}

TEST(Propagation, HeavierForkTriggersReorgAndPropagatesToPeer) {
    uint16_t port_a = next_test_port();
    uint16_t port_b = next_test_port();

    Blockchain chain_a;
    Mempool mempool_a;
    OrphanPool orphan_pool_a;
    Ledger ledger_a;
    Node node_a(port_a, make_version(port_a), chain_a, mempool_a, orphan_pool_a, ledger_a);
    ASSERT_TRUE(node_a.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    Blockchain chain_b;
    Mempool mempool_b;
    OrphanPool orphan_pool_b;
    Ledger ledger_b;
    Node node_b(port_b, make_version(port_b), chain_b, mempool_b, orphan_pool_b, ledger_b);
    ASSERT_TRUE(node_b.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_TRUE(node_b.connect_to_peer("127.0.0.1", port_a));
    ASSERT_TRUE(WaitUntil([&] { return node_a.peer_count() >= 1; }, std::chrono::seconds(1)));

    RawPeer source;
    ASSERT_TRUE(source.connect(port_a, make_version(port_a)));
    ASSERT_TRUE(WaitUntil([&] { return node_a.peer_count() >= 2; }, std::chrono::seconds(1)));

    HashBytes genesisHash = chain_a.latest().hash_;

    constexpr uint32_t kWeakDifficulty = 6;
    Block weakBlock = mine_block(1, genesisHash, 1700000000, kWeakDifficulty, {});
    ASSERT_TRUE(source.send(MessageType::BLOCK, weakBlock.serialize()));
    ASSERT_TRUE(WaitUntil([&] { return chain_a.has_block(weakBlock.hash_); }, std::chrono::seconds(2)))
        << "Node A never accepted the initial weak block";
    ASSERT_TRUE(WaitUntil([&] { return chain_b.has_block(weakBlock.hash_); }, std::chrono::seconds(2)))
        << "Node B never received the initial weak block";

    constexpr uint32_t kHeavyDifficulty = 12;
    Block heavyBlock = mine_block(1, genesisHash, 1700000001, kHeavyDifficulty, {});
    ASSERT_NE(heavyBlock.hash_, weakBlock.hash_);
    ASSERT_TRUE(source.send(MessageType::BLOCK, heavyBlock.serialize()));

    ASSERT_TRUE(WaitUntil([&] { return chain_a.latest().hash_ == heavyBlock.hash_; }, std::chrono::seconds(2)))
        << "Node A never reorganized onto the heavier fork";
    EXPECT_FALSE(chain_a.has_block(weakBlock.hash_))
        << "Node A's weak block should have been discarded by the reorg";

    ASSERT_TRUE(WaitUntil([&] { return chain_b.latest().hash_ == heavyBlock.hash_; }, std::chrono::seconds(2)))
        << "Node B never received the heavier block via post-reorg broadcast";
}

TEST(Propagation, ReorgReturnsDiscardedTransactionToMempool) {
    uint16_t port_a = next_test_port();
    uint16_t port_b = next_test_port();

    Blockchain chain_a;
    Mempool mempool_a;
    OrphanPool orphan_pool_a;
    Ledger ledger_a;
    Node node_a(port_a, make_version(port_a), chain_a, mempool_a, orphan_pool_a, ledger_a);
    ASSERT_TRUE(node_a.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    Blockchain chain_b;
    Mempool mempool_b;
    OrphanPool orphan_pool_b;
    Ledger ledger_b;
    Node node_b(port_b, make_version(port_b), chain_b, mempool_b, orphan_pool_b, ledger_b);
    ASSERT_TRUE(node_b.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_TRUE(node_b.connect_to_peer("127.0.0.1", port_a));
    ASSERT_TRUE(WaitUntil([&] { return node_a.peer_count() >= 1; }, std::chrono::seconds(1)));

    RawPeer source;
    ASSERT_TRUE(source.connect(port_a, make_version(port_a)));
    ASSERT_TRUE(WaitUntil([&] { return node_a.peer_count() >= 2; }, std::chrono::seconds(1)));

    HashBytes genesisHash = chain_a.latest().hash_;

    Wallet alice = make_wallet();

    ledger_a.set_balance(alice.address, 1000);

    Transaction orphanedTx = make_signed_tx(alice, "bob-address", 100);
    auto orphanedTxHash = orphanedTx.compute_hash();

    constexpr uint32_t kWeakDifficulty = 6;
    Block weakBlock = mine_block(1, genesisHash, 1700000000, kWeakDifficulty, {orphanedTx});
    ASSERT_TRUE(source.send(MessageType::BLOCK, weakBlock.serialize()));
    ASSERT_TRUE(WaitUntil([&] { return chain_a.has_block(weakBlock.hash_); }, std::chrono::seconds(2)));

    constexpr uint32_t kHeavyDifficulty = 12;
    Block heavyBlock = mine_block(1, genesisHash, 1700000001, kHeavyDifficulty, {});
    ASSERT_TRUE(source.send(MessageType::BLOCK, heavyBlock.serialize()));

    ASSERT_TRUE(WaitUntil([&] { return chain_a.latest().hash_ == heavyBlock.hash_; }, std::chrono::seconds(2)))
        << "Node A never reorganized onto the heavier fork";

    ASSERT_TRUE(WaitUntil([&] { return mempool_a.has_transaction(orphanedTxHash); }, std::chrono::seconds(2)))
        << "Discarded transaction was never returned to Node A's mempool after reorg";
}

TEST(Propagation, LedgerBalanceUpdatesOnBothNodesAfterBlockWithRealTransaction) {
    uint16_t port_a = next_test_port();
    uint16_t port_b = next_test_port();

    Blockchain chain_a;
    Mempool mempool_a;
    OrphanPool orphan_pool_a;
    Ledger ledger_a;
    Node node_a(port_a, make_version(port_a), chain_a, mempool_a, orphan_pool_a, ledger_a);
    ASSERT_TRUE(node_a.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    Blockchain chain_b;
    Mempool mempool_b;
    OrphanPool orphan_pool_b;
    Ledger ledger_b;
    Node node_b(port_b, make_version(port_b), chain_b, mempool_b, orphan_pool_b, ledger_b);
    ASSERT_TRUE(node_b.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_TRUE(node_b.connect_to_peer("127.0.0.1", port_a));
    ASSERT_TRUE(WaitUntil([&] { return node_a.peer_count() >= 1; }, std::chrono::seconds(1)));

    RawPeer source;
    ASSERT_TRUE(source.connect(port_a, make_version(port_a)));
    ASSERT_TRUE(WaitUntil([&] { return node_a.peer_count() >= 2; }, std::chrono::seconds(1)));

    Wallet alice = make_wallet();

    ledger_a.set_balance(alice.address, 1000);
    ledger_b.set_balance(alice.address, 1000);

    Transaction tx = make_signed_tx(alice, "bob-address", 250);

    Block block = mine_block(1, chain_a.latest().hash_, 1700000000, 8, {tx});
    ASSERT_TRUE(source.send(MessageType::BLOCK, block.serialize()));

    ASSERT_TRUE(WaitUntil([&] { return chain_a.has_block(block.hash_); }, std::chrono::seconds(2)))
        << "Node A never accepted the block";
    ASSERT_TRUE(WaitUntil([&] { return chain_b.has_block(block.hash_); }, std::chrono::seconds(2)))
        << "Node B never received the block";

    ASSERT_TRUE(WaitUntil([&] {
        auto balance = ledger_a.get_balance(alice.address);
        return balance.has_value() && *balance == 750u;
    }, std::chrono::seconds(2))) << "Node A's ledger was never updated by the transaction";

    ASSERT_TRUE(WaitUntil([&] {
        auto balance = ledger_b.get_balance(alice.address);
        return balance.has_value() && *balance == 750u;
    }, std::chrono::seconds(2))) << "Node B's ledger was never updated by the transaction";

    auto aliceBalanceB = ledger_b.get_balance(alice.address);
    auto bobBalanceB = ledger_b.get_balance("bob-address");
    ASSERT_TRUE(aliceBalanceB.has_value());
    ASSERT_TRUE(bobBalanceB.has_value());
    EXPECT_EQ(*aliceBalanceB, 750u);
    EXPECT_EQ(*bobBalanceB, 250u);
}

TEST(Propagation, BlockWithUnaffordableTransactionIsRejectedNotJustSkipped) {
    uint16_t port_a = next_test_port();
    uint16_t port_b = next_test_port();

    Blockchain chain_a;
    Mempool mempool_a;
    OrphanPool orphan_pool_a;
    Ledger ledger_a;
    Node node_a(port_a, make_version(port_a), chain_a, mempool_a, orphan_pool_a, ledger_a);
    ASSERT_TRUE(node_a.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    Blockchain chain_b;
    Mempool mempool_b;
    OrphanPool orphan_pool_b;
    Ledger ledger_b;
    Node node_b(port_b, make_version(port_b), chain_b, mempool_b, orphan_pool_b, ledger_b);
    ASSERT_TRUE(node_b.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_TRUE(node_b.connect_to_peer("127.0.0.1", port_a));
    ASSERT_TRUE(WaitUntil([&] { return node_a.peer_count() >= 1; }, std::chrono::seconds(1)));

    RawPeer source;
    ASSERT_TRUE(source.connect(port_a, make_version(port_a)));
    ASSERT_TRUE(WaitUntil([&] { return node_a.peer_count() >= 2; }, std::chrono::seconds(1)));

    Wallet alice = make_wallet();
    Transaction tx = make_signed_tx(alice, "bob-address", 250);

    size_t heightBefore = chain_a.size();
    Block block = mine_block(1, chain_a.latest().hash_, 1700000000, 8, {tx});
    ASSERT_TRUE(source.send(MessageType::BLOCK, block.serialize()));

    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    EXPECT_EQ(chain_a.size(), heightBefore)
        << "Node A accepted a block whose transaction its own Ledger couldn't apply";
    EXPECT_FALSE(chain_b.has_block(block.hash_))
        << "Node B received a block that A should never have relayed";
    EXPECT_FALSE(ledger_a.get_balance(alice.address).has_value())
        << "Ledger should be untouched -- no balance record should exist for alice";
}

TEST(Propagation, LedgerReflectsWinningBranchNotLosingBranchAfterReorg) {
    uint16_t port_a = next_test_port();
    uint16_t port_b = next_test_port();

    Blockchain chain_a;
    Mempool mempool_a;
    OrphanPool orphan_pool_a;
    Ledger ledger_a;
    Node node_a(port_a, make_version(port_a), chain_a, mempool_a, orphan_pool_a, ledger_a);
    ASSERT_TRUE(node_a.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    Blockchain chain_b;
    Mempool mempool_b;
    OrphanPool orphan_pool_b;
    Ledger ledger_b;
    Node node_b(port_b, make_version(port_b), chain_b, mempool_b, orphan_pool_b, ledger_b);
    ASSERT_TRUE(node_b.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    ASSERT_TRUE(node_b.connect_to_peer("127.0.0.1", port_a));
    ASSERT_TRUE(WaitUntil([&] { return node_a.peer_count() >= 1; }, std::chrono::seconds(1)));

    RawPeer source;
    ASSERT_TRUE(source.connect(port_a, make_version(port_a)));
    ASSERT_TRUE(WaitUntil([&] { return node_a.peer_count() >= 2; }, std::chrono::seconds(1)));

    HashBytes genesisHash = chain_a.latest().hash_;

    Wallet alice = make_wallet();
    ledger_a.set_balance(alice.address, 1000);
    ledger_b.set_balance(alice.address, 1000);

    Transaction txToBob = make_signed_tx(alice, "bob-address", 300);
    constexpr uint32_t kWeakDifficulty = 6;
    Block weakBlock = mine_block(1, genesisHash, 1700000000, kWeakDifficulty, {txToBob});
    ASSERT_TRUE(source.send(MessageType::BLOCK, weakBlock.serialize()));
    ASSERT_TRUE(WaitUntil([&] { return chain_a.has_block(weakBlock.hash_); }, std::chrono::seconds(2)));
    ASSERT_TRUE(WaitUntil([&] {
        auto b = ledger_a.get_balance(alice.address);
        return b.has_value() && *b == 700u;
    }, std::chrono::seconds(2))) << "Node A's ledger did not reflect the losing branch's tx before reorg";

    Transaction txToCarol = make_signed_tx(alice, "carol-address", 500);
    constexpr uint32_t kHeavyDifficulty = 12;
    Block heavyBlock = mine_block(1, genesisHash, 1700000001, kHeavyDifficulty, {txToCarol});
    ASSERT_NE(heavyBlock.hash_, weakBlock.hash_);
    ASSERT_TRUE(source.send(MessageType::BLOCK, heavyBlock.serialize()));

    ASSERT_TRUE(WaitUntil([&] { return chain_a.latest().hash_ == heavyBlock.hash_; }, std::chrono::seconds(2)))
        << "Node A never reorganized onto the heavier fork";

    Ledger groundTruth;
    groundTruth.set_balance(alice.address, 1000);
    ASSERT_TRUE(groundTruth.apply_transaction(txToCarol));

    ASSERT_TRUE(WaitUntil([&] {
        auto b = ledger_a.get_balance(alice.address);
        return b.has_value() && *b == *groundTruth.get_balance(alice.address);
    }, std::chrono::seconds(2))) << "Node A's ledger alice balance does not match winning-branch-only ground truth";

    auto aliceBalance = ledger_a.get_balance(alice.address);
    auto bobBalance = ledger_a.get_balance("bob-address");
    auto carolBalance = ledger_a.get_balance("carol-address");

    ASSERT_TRUE(aliceBalance.has_value());
    EXPECT_EQ(*aliceBalance, *groundTruth.get_balance(alice.address));
    EXPECT_EQ(*aliceBalance, 500u);

    ASSERT_TRUE(bobBalance.has_value())
        << "Node A's ledger still shows a balance for bob-address, but that "
           "payment was only in the losing (discarded) branch";

    ASSERT_TRUE(carolBalance.has_value());
    EXPECT_EQ(*carolBalance, *groundTruth.get_balance("carol-address"));
    EXPECT_EQ(*carolBalance, 500u);
}
