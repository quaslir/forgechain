#include "core/Block.hpp"
#include "core/Blockchain.hpp"
#include "core/Ledger.hpp"
#include "core/Mempool.hpp"
#include "core/OrphanPool.hpp"
#include "core/Transaction.hpp"
#include "crypto/Address.hpp"
#include "crypto/CommonTypes.hpp"
#include "crypto/Hash.hpp"
#include "crypto/Keys.hpp"
#include "crypto/Signature.hpp"
#include "network/Handshake.hpp"
#include "network/Node.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <string>

#define private public
#include "app/RpcServer.hpp"
#undef private

using namespace forgechain::app;
using namespace forgechain::core;
using namespace forgechain::crypto;
using namespace forgechain::network;

namespace {

struct TestNode : Node {
  Blockchain chain;
  Mempool mempool;
  OrphanPool orphan_pool;
  Ledger ledger;

  TestNode()
      : Node(0,
             VersionInfo{.protocol_version = 1, .chain_height = 0, .timestamp = 0, .listen_port = 0},
             chain, mempool, orphan_pool, ledger) {}
};

struct TestWallet {
  KeyPair keys;
  str address;
};

TestWallet make_test_wallet() {
  KeyPair kp = generate_keypair();
  return TestWallet{kp, derive_address(kp.public_key)};
}

Transaction make_signed_test_tx(const TestWallet &sender, const str &recipient,
                                 uint64_t amount) {
  Transaction tx(sender.address, recipient, amount, sender.keys.public_key);
  tx.signature_ = sign(tx.serialize_for_signing(), sender.keys.private_key);
  return tx;
}

RpcServer make_server(TestNode &node) { return RpcServer(node, 0); }

} // namespace

TEST(RpcServer, GetBalanceReturnsAmountForKnownAddress) {
  TestNode node;
  node.ledger.set_balance("alice-address", 500);
  RpcServer server = make_server(node);

  EXPECT_EQ(server.handle_command("GETBALANCE alice-address"), "500");
}

TEST(RpcServer, GetBalanceReturnsUnknownForUnknownAddress) {
  TestNode node;
  RpcServer server = make_server(node);

  EXPECT_EQ(server.handle_command("GETBALANCE nobody"), "UNKNOWN");
}

TEST(RpcServer, GetBalanceWithNoAddressReturnsError) {
  TestNode node;
  RpcServer server = make_server(node);

  EXPECT_EQ(server.handle_command("GETBALANCE"), "ERROR_EMPTY_ADDRESS");
}

TEST(RpcServer, GetBalanceOfZeroIsDistinctFromUnknown) {
  TestNode node;
  node.ledger.set_balance("alice-address", 0);
  RpcServer server = make_server(node);

  EXPECT_EQ(server.handle_command("GETBALANCE alice-address"), "0");
}

TEST(RpcServer, HeightReturnsCurrentChainHeight) {
  TestNode node;
  RpcServer server = make_server(node);

  EXPECT_EQ(server.handle_command("HEIGHT"), "1");
}

TEST(RpcServer, PeersReturnsZeroWhenNoPeersConnected) {
  TestNode node;
  RpcServer server = make_server(node);

  EXPECT_EQ(server.handle_command("PEERS"), "0");
}

TEST(RpcServer, UnknownCommandReturnsError) {
  TestNode node;
  RpcServer server = make_server(node);

  EXPECT_EQ(server.handle_command("FROBNICATE"), "ERROR_UNKNOWN_COMMAND");
}

TEST(RpcServer, EmptyLineReturnsError) {
  TestNode node;
  RpcServer server = make_server(node);

  EXPECT_EQ(server.handle_command(""), "ERROR_UNKNOWN_COMMAND");
}

TEST(RpcServer, SubmitTxWithNoPayloadReturnsError) {
  TestNode node;
  RpcServer server = make_server(node);

  EXPECT_EQ(server.handle_command("SUBMITTX"), "ERROR_EMPTY_PAYLOAD");
}

TEST(RpcServer, SubmitTxWithInvalidHexReturnsError) {
  TestNode node;
  RpcServer server = make_server(node);

  EXPECT_EQ(server.handle_command("SUBMITTX not-hex-at-all!!"),
            "ERROR_INVALID_HEX");
}

TEST(RpcServer, SubmitTxWithOddLengthHexReturnsError) {
  TestNode node;
  RpcServer server = make_server(node);

  EXPECT_EQ(server.handle_command("SUBMITTX abc"), "ERROR_INVALID_HEX");
}

TEST(RpcServer, SubmitTxWithValidHexButGarbagePayloadReturnsError) {
  TestNode node;
  RpcServer server = make_server(node);

  EXPECT_EQ(server.handle_command("SUBMITTX deadbeef"), "ERROR_INVALID_PAYLOAD");
}

TEST(RpcServer, SubmitTxWithValidSignedTransactionSucceeds) {
  TestNode node;
  TestWallet alice = make_test_wallet();
  node.ledger.set_balance(alice.address, 1000);

  Transaction tx = make_signed_test_tx(alice, "bob-address", 100);
  str hex = to_hex(tx.serialize());
  RpcServer server = make_server(node);

  EXPECT_EQ(server.handle_command("SUBMITTX " + hex), "OK");
  EXPECT_TRUE(node.mempool.has_transaction(tx.compute_hash()));
}

TEST(RpcServer, SubmitTxWithInvalidSignatureIsAcceptedByRpcButRejectedByMempool) {
  TestNode node;
  TestWallet alice = make_test_wallet();
  TestWallet mallory = make_test_wallet();
  node.ledger.set_balance(alice.address, 1000);

  Transaction tx(alice.address, "bob-address", 100, alice.keys.public_key);
  tx.signature_ = sign(tx.serialize_for_signing(), mallory.keys.private_key);
  str hex = to_hex(tx.serialize());
  RpcServer server = make_server(node);

  EXPECT_EQ(server.handle_command("SUBMITTX " + hex), "OK");
  EXPECT_FALSE(node.mempool.has_transaction(tx.compute_hash()))
      << "RPC reports OK because submit_transaction() has no return value -- "
         "the tx itself is still correctly rejected by Mempool underneath, "
         "this test documents that OK only means \"accepted for processing\", "
         "not \"confirmed in mempool\"";
}

TEST(RpcServer, CommandsAreCaseSensitive) {
  TestNode node;
  RpcServer server = make_server(node);

  EXPECT_EQ(server.handle_command("height"), "ERROR_UNKNOWN_COMMAND");
  EXPECT_EQ(server.handle_command("getbalance alice"), "ERROR_UNKNOWN_COMMAND");
}

TEST(RpcServer, GetBalanceIgnoresExtraTrailingArguments) {
  TestNode node;
  node.ledger.set_balance("alice-address", 42);
  RpcServer server = make_server(node);

  EXPECT_EQ(server.handle_command("GETBALANCE alice-address extra garbage"),
            "42");
}

TEST(RpcServer, LeadingWhitespaceBeforeCommandIsTolerated) {
  TestNode node;
  RpcServer server = make_server(node);

  EXPECT_EQ(server.handle_command("   HEIGHT"), "1");
}
