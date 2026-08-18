#pragma once

#include "core/Block.hpp"
#include "core/Blockchain.hpp"
#include "core/Mempool.hpp"
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
#include <thread>
#include <vector>
namespace forgechain::network {
struct PeerEntry {
  std::unique_ptr<Peer> peer;
  std::thread worker;
};
using VectorPeers = std::vector<PeerEntry>;
constexpr std::chrono::milliseconds CLEANER_TIMEOUT =
    std::chrono::milliseconds(500);
class Node {
public:
  Node(uint16_t listen_port, VersionInfo info, core::Blockchain &blockchain,
       core::Mempool &mempool);
  ~Node();
  bool start();
  void stop();
  bool accept_one_peer();
  void accept_loop();
  void peer_loop(Peer *peer);
  void cleaner_loop();
  bool connect_to_peer(const crypto::str &host, uint16_t port);

  [[nodiscard]] size_t peer_count() const;

private:
  bool send_msg(Peer *peer, MessageType type, const crypto::bytes &payload);
  [[nodiscard]] bool
  is_valid_new_block_unlocked(const core::Block &block) const;
  bool register_new_peer(TcpSocket &&socket);
  void broadcast_inv(Peer *exclude, InventoryItemType type,
                     const crypto::HashBytes &hash);
  void handle_inv(Peer *peer, const crypto::bytes &payload);
  void handle_getdata(Peer *peer, const crypto::bytes &payload);
  void handle_block(Peer *peer, const crypto::bytes &payload);
  void handle_tx(Peer *peer, const crypto::bytes &payload);
  void handle_getblocks(Peer * peer, const crypto::bytes& payload);
  uint16_t listen_port_;
  VersionInfo info_;
  TcpSocket listener_{-1};
  core::Blockchain &blockchain_;
  core::Mempool &mempool_;
  VectorPeers peers_;
  std::atomic<bool> running_{false};
  std::thread accept_thread_;
  std::thread cleaner_thread_;
  mutable std::mutex peers_mutex_;
  mutable std::mutex chain_mutex_;
};
} // namespace forgechain::network
