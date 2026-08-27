#pragma once

#include "core/Block.hpp"
#include "core/Blockchain.hpp"
#include "core/ForkResolution.hpp"
#include "core/Ledger.hpp"
#include "core/Mempool.hpp"
#include "core/OrphanPool.hpp"
#include "core/Transaction.hpp"
#include "crypto/CommonTypes.hpp"
#include "network/Handshake.hpp"
#include "network/Inventory.hpp"
#include "network/Message.hpp"
#include "network/Peer.hpp"
#include "network/TcpSocket.hpp"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>
namespace forgechain::network {

struct PeerEntry {
  std::shared_ptr<Peer> peer;
  std::thread worker;
};
using VectorPeers = std::vector<PeerEntry>;
constexpr std::chrono::milliseconds CLEANER_TIMEOUT =
    std::chrono::milliseconds(500);
constexpr auto PING_INTERVAL = std::chrono::seconds(1);
constexpr auto PING_TIMEOUT = std::chrono::seconds(45);
class Node {
public:
  Node(uint16_t listen_port, VersionInfo info, core::Blockchain &blockchain,
       core::Mempool &mempool, core::OrphanPool &orphan_pool,
       core::Ledger &ledger);
  ~Node();
  bool start();
  void stop();
  bool accept_one_peer();
  void accept_loop();
  bool connect_to_peer(const crypto::str &host, uint16_t port);

  [[nodiscard]] size_t peer_count() const;
  void submit_block(const core::Block &block);
  void submit_transaction(const core::Transaction &tx);
  [[nodiscard]] size_t chain_height() const;
  [[nodiscard]] std::optional<uint64_t>
  get_balance(const crypto::str &address) const;
  [[nodiscard]] crypto::HashBytes latest_hash() const;
  [[nodiscard]] std::vector<core::Transaction>
  transactions_for_block(size_t limit) const;
  [[nodiscard]] std::vector<core::Transaction> mempool_snapshot() const;
  void set_balance(const crypto::str &address, uint64_t amount);

private:
  void peer_loop(Peer *peer);
  void cleaner_loop();
  void ping_loop();
  bool send_msg(Peer *peer, MessageType type, const crypto::bytes &payload);

  bool register_new_peer(TcpSocket &&socket, const crypto::str &host,
                         bool is_outbound);
  void broadcast_inv(Peer *exclude, InventoryItemType type,
                     const crypto::HashBytes &hash);
  void handle_inv(Peer *peer, const crypto::bytes &payload);
  void handle_getdata(Peer *peer, const crypto::bytes &payload);
  void handle_block(Peer *peer, const crypto::bytes &payload);
  void handle_tx(Peer *peer, const crypto::bytes &payload);
  void handle_getblocks(Peer *peer, const crypto::bytes &payload);
  void handle_ping(Peer *peer);
  void handle_pong();
  void handle_peers(const crypto::bytes &payload);
  [[nodiscard]] bool apply_block_to_ledger(const core::Block &block);

  std::optional<std::vector<crypto::HashBytes>>
  try_reorg(core::ForkChain &&fork_chain);
  uint16_t listen_port_;
  VersionInfo info_;
  TcpSocket listener_{-1};
  core::Blockchain &blockchain_;
  core::Mempool &mempool_;
  core::OrphanPool &orphan_pool_;
  core::Ledger &ledger_;
  VectorPeers peers_;
  std::atomic<bool> running_{false};
  std::thread accept_thread_;
  std::thread cleaner_thread_;
  std::thread ping_thread_;
  mutable std::mutex peers_mutex_;
  mutable std::mutex chain_mutex_;
  mutable std::mutex orphan_mutex_;
};
} // namespace forgechain::network
