#pragma once

#include "chain/ChainManager.hpp"
#include "core/Block.hpp"
#include "core/Transaction.hpp"
#include "crypto/CommonTypes.hpp"
#include "network/AddressBook.hpp"
#include "network/Handshake.hpp"
#include "network/Inventory.hpp"
#include "network/Message.hpp"
#include "network/Peer.hpp"
#include "network/PeerAddress.hpp"
#include "network/TcpSocket.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>
namespace forgechain::network {

struct PeerEntry {
  std::shared_ptr<Peer> peer;
  std::thread worker;

  bool is_outbound{false};
};

struct Sync {
  std::chrono::steady_clock::time_point last_sync{
      std::chrono::steady_clock::now()};
  size_t sync_cursor{0};
};

using VectorPeers = std::vector<PeerEntry>;
constexpr std::chrono::milliseconds CLEANER_TIMEOUT =
    std::chrono::milliseconds(500);
constexpr auto PING_INTERVAL = std::chrono::seconds(5);
constexpr auto PING_TIMEOUT = std::chrono::seconds(40);
constexpr auto SYNC_INTERVAL = std::chrono::seconds(8);
constexpr auto CONNECT_INTERVAL = std::chrono::milliseconds(1000);
constexpr size_t TARGET_OUTBOUND_PEERS = 8;
constexpr auto GOSSIP_INTERVAL = std::chrono::seconds(30);
constexpr size_t MAX_BLOCKS_PER_RESPONSE = 2000;
class Node {
public:
  Node(uint16_t listen_port, VersionInfo info, chain::ChainManager &chain);
  ~Node();
  bool start();
  void stop();
  bool accept_one_peer();
  void accept_loop();
  bool connect_to_peer(const crypto::str &host, uint16_t port);

  [[nodiscard]] std::vector<crypto::str> peers() const;
  [[nodiscard]] size_t peer_count() const;
  [[nodiscard]] std::vector<crypto::str> book() const;

  void remember_peer(const crypto::str &host, uint16_t port);
  void set_logger(
      std::function<void(const crypto::str &, const crypto::str &)> logger);
  void submit_block(const core::Block &block);
  void submit_transaction(const core::Transaction &tx);

private:
  void peer_loop(std::shared_ptr<Peer> peer_owner);
  void cleaner_loop();
  void ping_loop();
  void connect_loop();
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
  void handle_peers(Peer *peer, const crypto::bytes &payload);

  [[nodiscard]] bool already_connected(const crypto::str &host,
                                       uint16_t port) const;
  [[nodiscard]] size_t outbound_peer_count() const;
  void gossip_peers();
  void send_peer_list(Peer *peer, const PeerAddress &peer_addr);
  void request_sync();
  bool wait_or_stop(std::chrono::milliseconds duration);
  uint16_t listen_port_;
  VersionInfo info_;
  TcpSocket listener_{-1};
  chain::ChainManager &chain_;
  VectorPeers peers_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stopping_{false};
  std::thread accept_thread_;
  std::thread cleaner_thread_;
  std::thread ping_thread_;
  std::thread connect_thread_;
  mutable std::mutex peers_mutex_;
  mutable std::mutex shutdown_mutex_;
  std::condition_variable shutdown_cv_;
  AddressBook address_book_;
  std::function<void(const crypto::str &, const crypto::str &)> logger_;
  Sync sync_{};
};
} // namespace forgechain::network
