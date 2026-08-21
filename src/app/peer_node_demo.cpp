// Hard/verbose demo executable for manually exercising the full P2P stack:
// initial sync (GETBLOCKS), block/tx propagation (INV/GETDATA/BLOCK/TX),
// and the PING/PONG heartbeat -- between two real Node objects talking over
// real TCP sockets on localhost.
//
// Built twice as `peer_a` and `peer_b` (same source, see app/CMakeLists.txt)
// so two instances can run in two separate terminals.
//
// Usage:
//   ./peer_a --port <PORT> [--connect <HOST> <PORT>] [--mine-every <SECONDS>]
//   ./peer_b --port <PORT> [--connect <HOST> <PORT>] [--mine-every <SECONDS>]
//
// Both peers mine by default (every 8s). Pass --mine-every 0 to disable
// mining on a given instance (pure listener).
//
// Example (two terminals, BOTH mining -- the hard/realistic case):
//   Terminal 1: ./peer_a --port 9000
//   Terminal 2: ./peer_b --port 9001 --connect 127.0.0.1 9000
//
// Because the chain is a simple linear vector (no fork/reorg support -- see
// Node::is_valid_new_block_unlocked), if both peers mine at nearly the same
// moment, whichever mined block arrives and is accepted FIRST wins; the
// other peer's own block gets rejected by the winner (prev_hash_ mismatch)
// once it arrives. This demo explicitly detects and logs that race instead
// of hiding it -- watch for [MINE] "... but height did NOT change" lines.
//
// Node has no public "mine and broadcast" API (see Node.hpp) -- handle_block
// and handle_tx only fire in response to an incoming network message. So to
// actually inject a freshly mined block/tx into the running Node, this demo
// connects to itself as a bare raw peer (like a third node would) and sends
// a BLOCK/TX message, exactly like PropagationTest.cpp's RawPeer does.
//
// State (chain height, mempool size, peer count) is only observed from the
// outside via polling -- Node.cpp itself is not touched or modified. That
// means PING/PONG traffic itself is invisible here (it's handled entirely
// inside Node); this demo can only infer heartbeat activity indirectly,
// via peer_count dropping if a peer is ever marked dead.

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
#include "crypto/Hash.hpp"
#include "crypto/Keys.hpp"
#include "crypto/Address.hpp"
#include "crypto/Signature.hpp"

#include "DemoLog.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

using namespace forgechain::network;
using namespace forgechain::core;
using namespace forgechain::consensus;
using namespace forgechain::crypto;
using forgechain::demo::DemoLog;

namespace {

std::atomic<bool> g_running{true};

void handle_sigint(int) { g_running.store(false); }

struct Args {
  uint16_t port = 0;
  std::string connect_host;
  uint16_t connect_port = 0;
  bool should_connect = false;
  int mine_every_seconds = 8;
};

Args parse_args(int argc, char **argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--port" && i + 1 < argc) {
      args.port = static_cast<uint16_t>(std::stoi(argv[++i]));
    } else if (arg == "--connect" && i + 2 < argc) {
      args.connect_host = argv[++i];
      args.connect_port = static_cast<uint16_t>(std::stoi(argv[++i]));
      args.should_connect = true;
    } else if (arg == "--mine-every" && i + 1 < argc) {
      args.mine_every_seconds = std::stoi(argv[++i]);
    }
  }
  return args;
}

VersionInfo make_version(uint64_t height) {
  return VersionInfo{.protocol_version = 1, .chain_height = height, .timestamp = 0};
}

class InjectorPeer {
public:
  bool connect(uint16_t port, DemoLog &log) {
    socket_ = std::make_unique<TcpSocket>(connect_to("127.0.0.1", port));
    if (!socket_->is_valid()) {
      log.log("INJECT", "failed to open local socket");
      return false;
    }
    auto remote = perform_handshake(socket_->fd(), make_version(0));
    if (!remote.has_value()) {
      log.log("INJECT", "handshake with own node failed");
      return false;
    }
    return true;
  }

  bool send(MessageType type, const bytes &payload) {
    Message msg{.type = type, .payload = payload};
    return send_message(socket_->fd(), msg);
  }

private:
  std::unique_ptr<TcpSocket> socket_;
};

struct Wallet {
  KeyPair keys;
  str address;
};

Wallet make_wallet() {
  KeyPair kp = generate_keypair();
  return Wallet{kp, derive_address(kp.public_key)};
}

Transaction make_signed_tx(const Wallet &sender, const str &recipient, uint64_t amount) {
  Transaction tx(sender.address, recipient, amount, sender.keys.public_key);
  tx.signature_ = sign(tx.serialize_for_signing(), sender.keys.private_key);
  return tx;
}

std::string short_hash(const HashBytes &h) {
  return to_hex(h).substr(0, 12) + "...";
}

} // namespace

int main(int argc, char **argv) {
  std::signal(SIGINT, handle_sigint);

  Args args = parse_args(argc, argv);
  if (args.port == 0) {
    std::cerr << "usage: " << argv[0]
              << " --port <PORT> [--connect <HOST> <PORT>] "
                 "[--mine-every <SECONDS>] (default mine-every: 8, 0 to disable)\n";
    return 1;
  }

  DemoLog log("NODE:" + std::to_string(args.port));

  log.log("BOOT", "==================================================");
  log.log("BOOT", "forgechain P2P demo starting");
  log.log("BOOT", "port=" + std::to_string(args.port) +
                   " mine_every=" +
                   (args.mine_every_seconds > 0
                        ? std::to_string(args.mine_every_seconds) + "s"
                        : "DISABLED"));
  log.log("BOOT", "==================================================");

  Blockchain chain;
  Mempool mempool;
  OrphanPool orphan_pool;
  Ledger ledger;
  Node node(args.port, make_version(0), chain, mempool, orphan_pool, ledger);

  log.log("BOOT", "starting Node::start()...");
  if (!node.start()) {
    log.log("BOOT", "FAILED to start node (port already in use?)");
    return 1;
  }
  log.log("BOOT", "node listening. genesis height=" +
                       std::to_string(chain.size()) + " hash=" +
                       short_hash(chain.latest().hash_));

  std::this_thread::sleep_for(std::chrono::milliseconds(200));

  if (args.should_connect) {
    log.log("PEER", "connecting to " + args.connect_host + ":" +
                          std::to_string(args.connect_port) + "...");
    if (node.connect_to_peer(args.connect_host, args.connect_port)) {
      log.log("PEER", "TCP connect + handshake succeeded");
    } else {
      log.log("PEER", "FAILED to connect (handshake or TCP failure)");
    }
  } else {
    log.log("PEER", "no --connect given; waiting for inbound connections");
  }

  constexpr uint32_t kDemoDifficulty = 8;
  Wallet demo_wallet = make_wallet();
  log.log("BOOT", "demo wallet address=" + demo_wallet.address);
  uint64_t tx_counter = 0;
  uint64_t mine_attempts = 0;
  uint64_t mine_races_lost = 0;

  size_t last_seen_peer_count = 0;
  size_t last_seen_chain_size = 0;
  size_t last_seen_mempool_size = 0;
  auto last_mine_time = std::chrono::steady_clock::now();
  auto last_status_time = std::chrono::steady_clock::now();

  while (g_running.load()) {
    size_t peers = node.peer_count();
    size_t height = chain.size();
    size_t mempool_size = mempool.size();

    if (peers != last_seen_peer_count) {
      if (peers < last_seen_peer_count) {
        log.log("PEER", "peer_count DROPPED: " +
                             std::to_string(last_seen_peer_count) + " -> " +
                             std::to_string(peers) +
                             "  (peer likely marked dead by heartbeat or "
                             "disconnected)");
      } else {
        log.log("PEER", "peer_count grew: " +
                             std::to_string(last_seen_peer_count) + " -> " +
                             std::to_string(peers));
      }
      last_seen_peer_count = peers;
    }
    if (height != last_seen_chain_size) {
      log.log("CHAIN", "height " + std::to_string(last_seen_chain_size) +
                            " -> " + std::to_string(height) +
                            "  latest_hash=" + short_hash(chain.latest().hash_) +
                            "  prev_hash=" +
                            short_hash(chain.latest().prev_hash_));
      last_seen_chain_size = height;
    }
    if (mempool_size != last_seen_mempool_size) {
      log.log("MEMPOOL", "size " + std::to_string(last_seen_mempool_size) +
                              " -> " + std::to_string(mempool_size));
      last_seen_mempool_size = mempool_size;
    }

    if (std::chrono::steady_clock::now() - last_status_time >
        std::chrono::seconds(5)) {
      log.log("STATUS", "peers=" + std::to_string(peers) +
                              " height=" + std::to_string(height) +
                              " mempool=" + std::to_string(mempool_size) +
                              " mine_attempts=" + std::to_string(mine_attempts) +
                              " races_lost=" + std::to_string(mine_races_lost));
      last_status_time = std::chrono::steady_clock::now();
    }

    if (args.mine_every_seconds > 0 &&
        std::chrono::steady_clock::now() - last_mine_time >
            std::chrono::seconds(args.mine_every_seconds)) {
      last_mine_time = std::chrono::steady_clock::now();
      ++mine_attempts;

      size_t height_before_mine = chain.size();
      HashBytes prev_hash_used = chain.latest().hash_;

      Block mined = mine_block(1, prev_hash_used,
                                static_cast<uint64_t>(std::time(nullptr)),
                                kDemoDifficulty, {});
      log.log("MINE", "#" + std::to_string(mine_attempts) +
                           " mined block on top of " +
                           short_hash(prev_hash_used) +
                           " -> new_hash=" + short_hash(mined.hash_));

      InjectorPeer block_injector;
      if (block_injector.connect(args.port, log)) {
        if (block_injector.send(MessageType::BLOCK, mined.serialize())) {
          log.log("MINE", "injected block into own node, awaiting accept...");
        } else {
          log.log("MINE", "FAILED to inject mined block (send error)");
        }
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(150));
      if (chain.size() == height_before_mine) {
        ++mine_races_lost;
        log.log("MINE", "#" + std::to_string(mine_attempts) +
                             " REJECTED: height did NOT change (" +
                             std::to_string(height_before_mine) +
                             " still). Someone else's block for this height "
                             "won the race first (prev_hash_ mismatch).");
      } else {
        log.log("MINE", "#" + std::to_string(mine_attempts) +
                             " ACCEPTED into own chain, height now " +
                             std::to_string(chain.size()));
      }

      Transaction tx =
          make_signed_tx(demo_wallet, "demo-recipient", 10 + tx_counter);
      ++tx_counter;
      log.log("MINE", "signed tx amount=" + std::to_string(10 + tx_counter - 1) +
                           " hash=" + short_hash(tx.compute_hash()));

      InjectorPeer tx_injector;
      if (tx_injector.connect(args.port, log)) {
        if (tx_injector.send(MessageType::TX, tx.serialize())) {
          log.log("MINE", "injected tx into own mempool");
        } else {
          log.log("MINE", "FAILED to inject tx (send error)");
        }
      }
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  log.log("SHUTDOWN", "SIGINT received, stopping node...");
  node.stop();
  log.log("SHUTDOWN", "stopped cleanly. final height=" +
                            std::to_string(chain.size()) +
                            " final mempool=" + std::to_string(mempool.size()));
  return 0;
}
