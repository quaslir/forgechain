#include "network/Node.hpp"
#include "network/Handshake.hpp"
#include "core/Blockchain.hpp"
#include "core/Mempool.hpp"
#include "core/OrphanPool.hpp"
#include "core/Ledger.hpp"
#include "crypto/Address.hpp"
#include "crypto/CommonTypes.hpp"
#include "crypto/Keys.hpp"
#include "crypto/Signature.hpp"

#include <gtest/gtest.h>

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <thread>
#include <vector>
#include <cstddef>
#include <optional>
#include "core/Transaction.hpp"
using namespace forgechain::network;
using namespace forgechain::core;
using namespace forgechain::crypto;

namespace {

uint16_t next_test_port() {
    static auto port = static_cast<uint16_t>(32000 + (getpid() % 1000) * 20);
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

struct TestNode : Node {
    Blockchain chain;
    Mempool mempool;
    OrphanPool orphan_pool;
    Ledger ledger;

    TestNode(uint16_t port, VersionInfo info)
        : Node(port, info, chain, mempool, orphan_pool, ledger) {}
};

::testing::AssertionResult RunWithTimeout(std::chrono::milliseconds timeout,
                                           const std::function<void()>& fn) {
    auto fut = std::async(std::launch::async, fn);
    if (fut.wait_for(timeout) != std::future_status::ready) {
        return ::testing::AssertionFailure()
               << "operation did not complete within " << timeout.count() << "ms (hung)";
    }
    fut.get();
    return ::testing::AssertionSuccess();
}

size_t WaitForPeerCount(const Node& node, size_t expected, std::chrono::milliseconds timeout) {
    auto start = std::chrono::steady_clock::now();
    size_t count = node.peer_count();
    while (count != expected && std::chrono::steady_clock::now() - start < timeout) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        count = node.peer_count();
    }
    return count;
}

}  // namespace


TEST(Node, StartOnFreshPortSucceeds) {
    uint16_t node_port = next_test_port();
    TestNode node(node_port, make_version(node_port));
    EXPECT_TRUE(node.start());
}

TEST(Node, PeerCountStartsAtZero) {
    uint16_t node_port = next_test_port();
    TestNode node(node_port, make_version(node_port));
    ASSERT_TRUE(node.start());
    EXPECT_EQ(node.peer_count(), 0u);
}

TEST(Node, ClientConnectsAndBothSidesRegisterPeer) {
    uint16_t port = next_test_port();
    TestNode server(port, make_version(port, 5));
    ASSERT_TRUE(server.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    TestNode client(0, make_version(0, 3));
    ASSERT_TRUE(client.connect_to_peer("127.0.0.1", port));

    EXPECT_EQ(WaitForPeerCount(server, 1, std::chrono::seconds(1)), 1u);
    EXPECT_EQ(client.peer_count(), 1u);
}

TEST(Node, MultiplePeersConnectSequentially) {
    uint16_t port = next_test_port();
    TestNode server(port, make_version(port));
    ASSERT_TRUE(server.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    constexpr int kNumClients = 5;
    std::vector<std::unique_ptr<TestNode>> clients;
    for (int i = 0; i < kNumClients; ++i) {
        clients.push_back(std::make_unique<TestNode>(0, make_version(0, static_cast<uint64_t>(i))));
        ASSERT_TRUE(clients.back()->connect_to_peer("127.0.0.1", port));
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    EXPECT_EQ(WaitForPeerCount(server, kNumClients, std::chrono::seconds(1)),
              static_cast<size_t>(kNumClients));
    for (auto& c : clients) {
        EXPECT_EQ(c->peer_count(), 1u);
    }
}

TEST(Node, ConnectToUnusedPortFailsQuickly) {
    TestNode client(0, make_version(0));
    auto start = std::chrono::steady_clock::now();
    bool connected = client.connect_to_peer("127.0.0.1", next_test_port());
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(connected);
    EXPECT_LT(elapsed, std::chrono::seconds(2));
    EXPECT_EQ(client.peer_count(), 0u);
}

TEST(Node, SecondStartOnSamePortFailsWithoutCorruptingFirst) {
    uint16_t port = next_test_port();
    TestNode node_a(port, make_version(port));
    ASSERT_TRUE(node_a.start());

    TestNode node_b(port, make_version(port));
    EXPECT_FALSE(node_b.start());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    TestNode client(0, make_version(0));
    EXPECT_TRUE(client.connect_to_peer("127.0.0.1", port));
}


TEST(Node, DestructorDoesNotHangWithNoConnections) {
    EXPECT_TRUE(RunWithTimeout(std::chrono::seconds(3), [] {
        uint16_t node_port = next_test_port();
    TestNode node(node_port, make_version(node_port));
        ASSERT_TRUE(node.start());
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }));
}

TEST(Node, ImmediateDestructionRightAfterStartDoesNotHang) {
    for (int i = 0; i < 10; ++i) {
        EXPECT_TRUE(RunWithTimeout(std::chrono::seconds(2), [] {
            uint16_t node_port = next_test_port();
    TestNode node(node_port, make_version(node_port));
            node.start();
        })) << "iteration " << i;
    }
}

TEST(Node, StopCanBeCalledMultipleTimesSafely) {
    uint16_t node_port = next_test_port();
    TestNode node(node_port, make_version(node_port));
    ASSERT_TRUE(node.start());
    EXPECT_TRUE(RunWithTimeout(std::chrono::seconds(2), [&] {
        node.stop();
        node.stop();
        node.stop();
    }));
}

TEST(Node, DestructorDoesNotHangWithActiveConnectedPeer) {
    uint16_t port = next_test_port();
    EXPECT_TRUE(RunWithTimeout(std::chrono::seconds(3), [&] {
        TestNode server(port, make_version(port));
        server.start();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        TestNode client(0, make_version(0));
        client.connect_to_peer("127.0.0.1", port);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        ASSERT_EQ(server.peer_count(), 1u);

    }));
}

TEST(Node, PeerVanishingMidHandshakeDoesNotHangAcceptThread) {
    uint16_t port = next_test_port();
    EXPECT_TRUE(RunWithTimeout(std::chrono::seconds(10), [&] {
        TestNode server(port, make_version(port));
        ASSERT_TRUE(server.start());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        for (int i = 0; i < 10; ++i) {
            TestNode client(0, make_version(0, static_cast<uint64_t>(i)));
            client.connect_to_peer("127.0.0.1", port);
        }
    }));
}


TEST(Node, ConcurrentSimultaneousConnectionsAllSucceed) {
    uint16_t port = next_test_port();
    TestNode server(port, make_version(port));
    ASSERT_TRUE(server.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    constexpr int kNumClients = 10;
    std::vector<std::unique_ptr<TestNode>> clients;
    for (int i = 0; i < kNumClients; ++i) {
        clients.push_back(std::make_unique<TestNode>(0, make_version(0, static_cast<uint64_t>(i))));
    }

    std::vector<std::thread> connectors;
    std::atomic<int> success_count{0};
    for (int i = 0; i < kNumClients; ++i) {
        connectors.emplace_back([&clients, i, &success_count, port]() {
            if (clients[static_cast<size_t>(i)]->connect_to_peer("127.0.0.1", port)) {
                ++success_count;
            }
        });
    }
    for (auto& t : connectors) t.join();

    EXPECT_EQ(success_count.load(), kNumClients);
    EXPECT_EQ(WaitForPeerCount(server, kNumClients, std::chrono::seconds(1)),
              static_cast<size_t>(kNumClients));
}

TEST(Node, ParallelChurnAcrossMultipleThreadsSettlesToZero) {

    uint16_t port = next_test_port();
    TestNode server(port, make_version(port));
    ASSERT_TRUE(server.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    constexpr int kThreads = 6;
    constexpr int kRoundsPerThread = 6;
    std::atomic<int> total_connected{0};

    EXPECT_TRUE(RunWithTimeout(std::chrono::seconds(20), [&] {
        std::vector<std::thread> churners;
        for (int t = 0; t < kThreads; ++t) {
            churners.emplace_back([&, t]() {
                for (int r = 0; r < kRoundsPerThread; ++r) {
                    TestNode client(0, make_version(0, static_cast<uint64_t>(t * 1000 + r)));
                    if (client.connect_to_peer("127.0.0.1", port)) {
                        ++total_connected;
                    }
                }
            });
        }
        for (auto& th : churners) th.join();
    }));

    EXPECT_EQ(total_connected.load(), kThreads * kRoundsPerThread);
    EXPECT_EQ(WaitForPeerCount(server, 0, std::chrono::seconds(3)), 0u)
        << "cleaner_loop failed to reap all dead peers after churn";
}

TEST(Node, PeerCountReflectsZeroImmediatelyAfterStop) {
    uint16_t port = next_test_port();
    TestNode server(port, make_version(port));
    ASSERT_TRUE(server.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    TestNode client(0, make_version(0));
    ASSERT_TRUE(client.connect_to_peer("127.0.0.1", port));
    ASSERT_EQ(WaitForPeerCount(server, 1, std::chrono::seconds(1)), 1u);

    server.stop();
    EXPECT_EQ(server.peer_count(), 0u);
}


TEST(Node, RepeatedStartStopCyclesOnDifferentPortsDoNotLeakOrHang) {
    constexpr int kGenerations = 5;
    for (int gen = 0; gen < kGenerations; ++gen) {
        uint16_t port = next_test_port();
        EXPECT_TRUE(RunWithTimeout(std::chrono::seconds(5), [&] {
            TestNode server(port, make_version(port));
            ASSERT_TRUE(server.start());
            std::this_thread::sleep_for(std::chrono::milliseconds(50));

            TestNode client(0, make_version(0));
            ASSERT_TRUE(client.connect_to_peer("127.0.0.1", port));
            ASSERT_EQ(WaitForPeerCount(server, 1, std::chrono::seconds(1)), 1u);
        })) << "generation " << gen;
    }
}

namespace {

struct TestWallet {
    KeyPair keys;
    str address;
};

TestWallet make_test_wallet() {
    KeyPair kp = generate_keypair();
    return TestWallet{kp, derive_address(kp.public_key)};
}

Transaction make_signed_test_tx(const TestWallet& sender, const str& recipient,
                                 uint64_t amount, uint64_t fee = 0) {
    Transaction tx(sender.address, recipient, amount, sender.keys.public_key, fee);
    tx.signature_ = sign(tx.serialize_for_signing(), sender.keys.private_key);
    return tx;
}

}  // namespace

TEST(Node, TransactionsForBlockFiltersOutTransactionSenderCannotAfford) {
    TestNode server(0, make_version(0));
    TestWallet alice = make_test_wallet();
    TestWallet bob = make_test_wallet();
    server.ledger.set_balance(alice.address, 10);
    server.ledger.set_balance(bob.address, 100);

    Transaction affordable = make_signed_test_tx(bob, "charlie-address", 20);
    Transaction unaffordable = make_signed_test_tx(alice, "charlie-address", 500);
    server.mempool.add_transaction(affordable, bob.keys.public_key);
    server.mempool.add_transaction(unaffordable, alice.keys.public_key);

    auto selected = server.transactions_for_block(10);

    ASSERT_EQ(selected.size(), 1u);
    EXPECT_EQ(selected[0].sender_, bob.address);
}

TEST(Node, TransactionsForBlockKeepsBothWhenSequentiallyAffordable) {
    TestNode server(0, make_version(0));
    TestWallet alice = make_test_wallet();
    TestWallet bob = make_test_wallet();
    server.ledger.set_balance(alice.address, 100);
    server.ledger.set_balance(bob.address, 0);

    Transaction first = make_signed_test_tx(alice, bob.address, 60);
    Transaction second = make_signed_test_tx(bob, "charlie-address", 30);
    server.mempool.add_transaction(first, alice.keys.public_key);
    server.mempool.add_transaction(second, bob.keys.public_key);

    auto selected = server.transactions_for_block(10);

    ASSERT_EQ(selected.size(), 2u);
}

TEST(Node, TransactionsForBlockDoesNotMutateRealLedger) {
    TestNode server(0, make_version(0));
    TestWallet alice = make_test_wallet();
    TestWallet bob = make_test_wallet();
    server.ledger.set_balance(alice.address, 100);
    server.ledger.set_balance(bob.address, 0);

    Transaction tx = make_signed_test_tx(alice, bob.address, 60);
    server.mempool.add_transaction(tx, alice.keys.public_key);

    auto ignored = server.transactions_for_block(10);
    (void)ignored;

    EXPECT_EQ(server.ledger.get_balance(alice.address), std::optional<uint64_t>(100));
    EXPECT_EQ(server.ledger.get_balance(bob.address), std::optional<uint64_t>(0));
}

TEST(Node, TransactionsForBlockReturnsEmptyWhenMempoolEmpty) {
    TestNode server(0, make_version(0));

    auto selected = server.transactions_for_block(10);

    EXPECT_TRUE(selected.empty());
}

TEST(Node, TransactionsForBlockRespectsLimitAfterFiltering) {
    TestNode server(0, make_version(0));
    TestWallet alice = make_test_wallet();
    server.ledger.set_balance(alice.address, 1000);

    for (int i = 0; i < 5; ++i) {
        Transaction tx = make_signed_test_tx(alice, "recipient-address",
                                              static_cast<uint64_t>(10 + i));
        server.mempool.add_transaction(tx, alice.keys.public_key);
    }

    auto selected = server.transactions_for_block(2);

    EXPECT_EQ(selected.size(), 2u);
}
