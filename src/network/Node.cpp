#include "network/Node.hpp"
#include "chain/ChainManager.hpp"
#include "core/Block.hpp"
#include "core/Transaction.hpp"
#include "crypto/CommonTypes.hpp"
#include "network/AddressBook.hpp"
#include "network/GetBlocks.hpp"
#include "network/Handshake.hpp"
#include "network/Inventory.hpp"
#include "network/Message.hpp"
#include "network/Peer.hpp"
#include "network/PeerAddress.hpp"
#include "network/TcpSocket.hpp"
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <utility>
#include <vector>
namespace forgechain::network {
Node::Node(uint16_t listen_port, VersionInfo info, chain::ChainManager &chain)
    : listen_port_(listen_port), info_(info), chain_(chain) {}

bool Node::start() {
  listener_ = listen_on(listen_port_);
  if (!listener_.is_valid())
    return false;
  listener_.set_receive_timeout(1);
  running_.store(true);
  stopping_.store(false);
  accept_thread_ = std::thread(&Node::accept_loop, this);
  cleaner_thread_ = std::thread(&Node::cleaner_loop, this);
  ping_thread_ = std::thread(&Node::ping_loop, this);
  connect_thread_ = std::thread(&Node::connect_loop, this);
  return true;
}

void Node::accept_loop() {
  while (running_) {
    accept_one_peer();
  }
}
void Node::peer_loop(std::shared_ptr<Peer> peer_owner) {
  Peer *peer = peer_owner.get();
  while (running_ && peer->is_alive()) {
    Message msg;
    if (!receive_message(peer->socket().fd(), msg)) {
      peer->mark_dead();
      break;
    }
    peer->touch();
    switch (msg.type) {
    case MessageType::INV:
      handle_inv(peer, msg.payload);
      break;
    case MessageType::GETDATA:
      handle_getdata(peer, msg.payload);
      break;
    case MessageType::BLOCK:
      handle_block(peer, msg.payload);
      break;
    case MessageType::TX:
      handle_tx(peer, msg.payload);
      break;
    case MessageType::GETBLOCKS:
      handle_getblocks(peer, msg.payload);
      break;
    case MessageType::PING:
      handle_ping(peer);
      break;
    case MessageType::PONG:
      handle_pong();
      break;
    case MessageType::PEERS:

      handle_peers(peer, msg.payload);
      break;
    default:
      break;
    }
  }
}
void Node::cleaner_loop() {
  while (running_) {
    std::vector<std::thread> workers_to_join;
    {
      std::lock_guard<std::mutex> lock(peers_mutex_);
      std::erase_if(peers_, [&workers_to_join](PeerEntry &peer_entry) {
        if (peer_entry.peer->is_alive())
          return false;

        peer_entry.peer->socket().close_socket();
        if (peer_entry.worker.joinable()) {
          workers_to_join.push_back(std::move(peer_entry.worker));
        }
        return true;
      });
    }

    for (auto &worker : workers_to_join) {
      worker.join();
    }

    if(!wait_or_stop(std::chrono::duration_cast<std::chrono::milliseconds>(CLEANER_TIMEOUT))) break;
  }
}

void Node::ping_loop() {
  auto last_gossip = std::chrono::steady_clock::now();
  while (running_) {
    {
      std::lock_guard<std::mutex> lock(peers_mutex_);

      for (auto &peer_entry : peers_) {
        if (!peer_entry.peer->is_alive())
          continue;
        if (peer_entry.peer->elapsed() >= PING_TIMEOUT) {
          peer_entry.peer->mark_dead();
          continue;
        }
        if (peer_entry.peer->elapsed() >= PING_INTERVAL) {
          send_msg(peer_entry.peer.get(), MessageType::PING, {});
        }
      }
    }
    auto now = std::chrono::steady_clock::now();
    if(now - sync_.last_sync >= SYNC_INTERVAL) {
        request_sync();
        sync_.last_sync = now;
    }
    if (now - last_gossip >= GOSSIP_INTERVAL) {
      gossip_peers();
      last_gossip = now;
    }

    if(!wait_or_stop(std::chrono::duration_cast<std::chrono::milliseconds>(PING_INTERVAL))) break;
  }
}

void Node::connect_loop() {
  while (running_) {
    while (outbound_peer_count() < TARGET_OUTBOUND_PEERS) {
      auto candidate = address_book_.select_candidate();
      if (!candidate.has_value())
        break;
      if (already_connected(candidate->host, candidate->port)) {
        TcpSocket probe = connect_to(candidate->host, candidate->port);
        if (probe.is_valid()) {
          address_book_.mark_success(*candidate);
        } else {
          address_book_.mark_failure(*candidate);
        }
        continue;
      }
      TcpSocket socket = connect_to(candidate->host, candidate->port);
      bool connected =
          socket.is_valid() &&
          register_new_peer(std::move(socket), candidate->host, true);
      crypto::str endpoint =
          candidate->host + ":" + std::to_string(candidate->port);
      if (connected) {
        address_book_.mark_success(*candidate);
        if (logger_) {
          logger_("PEER", "connected to " + endpoint);
        }
      } else {
        address_book_.mark_failure(*candidate);
        if (logger_) {
          logger_("PEER", "dial failed " + endpoint);
        }
      }
    }

    if(!wait_or_stop(std::chrono::duration_cast<std::chrono::milliseconds>(CONNECT_INTERVAL))) break;
  }
}

bool Node::register_new_peer(TcpSocket &&socket, const crypto::str &host,
                             bool is_outbound) {
  if (!socket.is_valid())
    return false;
  socket.set_receive_timeout(5);
  auto my_height = static_cast<uint64_t>(chain_.chain_height());
  VersionInfo current_info = info_;
  current_info.chain_height = my_height;

  auto incoming_info = perform_handshake(socket.fd(), current_info);
  if (!incoming_info.has_value())
    return false;
  if (info_.node_id == incoming_info->node_id)
    return false;
  auto peer = std::make_shared<Peer>(std::move(socket), *incoming_info, host);
  Peer *raw_peer = peer.get();

  // sync between two peers
  if (my_height < incoming_info->chain_height) {
    if (!send_msg(raw_peer, MessageType::GETBLOCKS,
                  serialize_getblocks(my_height)))
      return false;
  }
  PeerAddress candidate{.host = host, .port = incoming_info->listen_port};
  bool source_is_local = !AddressBook::is_routable(candidate);
  bool accepted{true};
  {
    std::lock_guard<std::mutex> peers_lock(peers_mutex_);
    if (stopping_)
      return false;
    for (size_t i = 0; i < peers_.size(); i++) {
      if (!peers_[i].peer->is_alive() ||
          peers_[i].peer->remote_version().listen_port == 0)
        continue;
      if (peers_[i].peer->host() != host ||
          peers_[i].peer->remote_version().listen_port !=
              incoming_info->listen_port)
        continue;
      bool keep_new = (info_.node_id < incoming_info->node_id) == is_outbound;
      if (!keep_new) {
        accepted = false;
      } else {
        peers_[i].peer->mark_dead();
        peers_[i].peer->socket().close_socket();
      }
      break;
    }

    if (accepted) {

      peers_.push_back(
          PeerEntry{.peer = peer,
                    .worker = std::thread{&Node::peer_loop, this, peer},
                    .is_outbound = is_outbound});
    }
  }
  if (!accepted) {
    if (logger_) {
      logger_("PEER", "duplicate connection dropped " + host);
    }
    return false;
  }
  if (is_outbound) {
    address_book_.add(candidate, true);
    address_book_.mark_success(candidate);
  } else {
    address_book_.add(candidate, source_is_local);
    if (logger_) {
      logger_("PEER", "inbound peer " + host + ":" +
                          std::to_string(incoming_info->listen_port));
    }
  }

  send_peer_list(raw_peer, candidate);
  return true;
}

bool Node::accept_one_peer() {
  TcpSocket socket = accept_connection(listener_);
  auto host = get_peer_ip(socket.fd());
  if (!host.has_value()) {
    return false;
  }

  return register_new_peer(std::move(socket), *host, false);
}

bool Node::connect_to_peer(const crypto::str &host, uint16_t port) {
  if (port == listen_port_ &&
      (host == "127.0.0.1" || host == "localhost" || host == "0.0.0.0")) {
    return false;
  }

  if (already_connected(host, port))
    return true;
  address_book_.add(PeerAddress{.host = host, .port = port}, true);
  TcpSocket socket = connect_to(host, port);

  return register_new_peer(std::move(socket), host, true);
}
size_t Node::peer_count() const {
  std::lock_guard<std::mutex> lock(peers_mutex_);
  return peers_.size();
}
std::vector<crypto::str> Node::peers() const {
  std::lock_guard<std::mutex> lock(peers_mutex_);
  if (peers_.empty())
    return {};
  std::vector<crypto::str> result;
  for (const auto &peer : peers_) {
    if (!peer.peer->is_alive())
      continue;
    result.emplace_back(
        peer.peer->host() + ":" +
        std::to_string(peer.peer->remote_version().listen_port) +
        (peer.is_outbound ? " (out)" : " (in)"));
  }

  return result;
}

std::vector<crypto::str> Node::book() const {

  auto book = address_book_.snapshot();
  if (book.empty())
    return {};

  std::vector<crypto::str> result;
  result.reserve(book.size());
  for (const auto &entry : book) {
    result.emplace_back(
        entry.address.host + ":" + std::to_string(entry.address.port) +
        (entry.verified
             ? " verified"
             : " unverified (" + std::to_string(entry.failures) + " fails)"));
  }

  return result;
}

bool Node::send_msg(Peer *peer, MessageType type,
                    const crypto::bytes &payload) {
  Message msg{.type = type, .payload = payload};
  return send_message(peer->socket().fd(), msg);
}

void Node::broadcast_inv(Peer *exclude, InventoryItemType type,
                         const crypto::HashBytes &hash) {
  InventoryItem item{.type = type, .hash = hash};
  std::vector<InventoryItem> items{item};
  crypto::bytes payload = serialize_inventory(items);
  std::lock_guard<std::mutex> lock(peers_mutex_);

  for (const auto &peer_entry : peers_) {
    if (peer_entry.peer.get() == exclude || !peer_entry.peer->is_alive())
      continue;
    send_msg(peer_entry.peer.get(), MessageType::INV, payload);
  }
}

void Node::handle_inv(Peer *peer, const crypto::bytes &payload) {
  auto items = deserialize_inventory(payload);
  if (!items.has_value())
    return;

  std::vector<InventoryItem> missing;

  for (const auto &item : *items) {
    bool have_it = false;
    if (item.type == InventoryItemType::BLOCK) {
      have_it = chain_.has_block(item.hash);
    } else if (item.type == InventoryItemType::TRANSACTION) {
      have_it = chain_.has_transaction(item.hash);
    }

    if (!have_it) {
      missing.push_back(item);
    }
  }

  if (!missing.empty()) {
    crypto::bytes getdata_payload = serialize_inventory(missing);
    send_msg(peer, MessageType::GETDATA, getdata_payload);
  }
}

void Node::handle_getdata(Peer *peer, const crypto::bytes &payload) {

  auto items = deserialize_inventory(payload);
  if (!items.has_value())
    return;

  for (const auto &item : *items) {
    if (item.type == InventoryItemType::BLOCK) {
      auto block = chain_.find_block(item.hash);
      if (block.has_value()) {
        send_msg(peer, MessageType::BLOCK, block->serialize());
      }
    }

    else if (item.type == InventoryItemType::TRANSACTION) {
      auto tx = chain_.find_transaction(item.hash);
      if (tx.has_value()) {
        send_msg(peer, MessageType::TX, tx->serialize());
      }
    }
  }
}
void Node::handle_block(Peer *peer, const crypto::bytes &payload) {
  auto block_container = core::Block::deserialize(payload);
  if (!block_container.has_value())
    return;
  auto block_outcome = chain_.submit_block(*block_container);
  switch (block_outcome.status) {
  case chain::BlockOutcome::Status::Accepted:
  case chain::BlockOutcome::Status::Reorged:
    for (const auto &reorg_hash : block_outcome.to_broadcast) {
      broadcast_inv(peer, InventoryItemType::BLOCK, reorg_hash);
    }
    break;

  case chain::BlockOutcome::Status::NeedParent:
    if (peer && block_outcome.missing_parent.has_value()) {
      std::vector<InventoryItem> items{
          InventoryItem{.type = InventoryItemType::BLOCK,
                        .hash = *block_outcome.missing_parent}};
      send_msg(peer, MessageType::GETDATA, serialize_inventory(items));
    }
    break;
  case chain::BlockOutcome::Status::Rejected:

    break;
  }
}

void Node::handle_tx(Peer *peer, const crypto::bytes &payload) {
  auto tx_container = core::Transaction::deserialize(payload);
  if (!tx_container.has_value())
    return;
  if (!chain_.submit_transaction(*tx_container))
    return;
  broadcast_inv(peer, InventoryItemType::TRANSACTION,
                tx_container->compute_hash());
}

void Node::handle_getblocks(Peer *peer, const crypto::bytes &payload) {
  auto from_height_container = deserialize_getblocks(payload);
  if (!from_height_container.has_value())
    return;
  auto blocks = chain_.blocks_from(static_cast<size_t>(*from_height_container), MAX_BLOCKS_PER_RESPONSE);
  for (const auto &block : blocks) {
    send_msg(peer, MessageType::BLOCK, block.serialize());
  }
}

void Node::handle_ping(Peer *peer) { send_msg(peer, MessageType::PONG, {}); }
void Node::handle_pong() {}

void Node::handle_peers(Peer *peer, const crypto::bytes &payload) {
  auto list = deserialize_peer_list(payload);
  if (!list.has_value())
    return;
  bool source_is_local =
      peer != nullptr &&
      !AddressBook::is_routable(PeerAddress{
          .host = peer->host(), .port = peer->remote_version().listen_port});
  for (const auto &new_peer : *list) {
    address_book_.add(new_peer, source_is_local);
  }
}

void Node::submit_block(const core::Block &block) {
  auto outcome = chain_.submit_block(block);
  for (const auto &h : outcome.to_broadcast) {
    broadcast_inv(nullptr, InventoryItemType::BLOCK, h);
  }
}
void Node::submit_transaction(const core::Transaction &tx) {
  if (chain_.submit_transaction(tx)) {
    broadcast_inv(nullptr, InventoryItemType::TRANSACTION, tx.compute_hash());
  }
}

void Node::remember_peer(const crypto::str &host, uint16_t port) {
  address_book_.add(PeerAddress{.host = host, .port = port}, true);
}

void Node::set_logger(
    std::function<void(const crypto::str &, const crypto::str &)> logger) {
  logger_ = std::move(logger);
}

bool Node::already_connected(const crypto::str &host, uint16_t port) const {
  std::lock_guard<std::mutex> peers_lock(peers_mutex_);

  for (const auto &entry : peers_) {
    if (entry.peer->is_alive() && entry.peer->host() == host &&
        entry.peer->remote_version().listen_port == port)
      return true;
  }

  return false;
}
size_t Node::outbound_peer_count() const {
  size_t count{0};
  std::lock_guard<std::mutex> peers_lock(peers_mutex_);

  for (const auto &entry : peers_) {
    if (entry.is_outbound && entry.peer->is_alive())
      count++;
  }

  return count;
}

void Node::gossip_peers() {
  std::vector<std::shared_ptr<Peer>> targets;
  {
    std::lock_guard<std::mutex> peers_lock(peers_mutex_);

    for (const auto &entry : peers_) {
      if (entry.peer->is_alive())
        targets.push_back(entry.peer);
    }
  }

  for (const auto &target : targets) {
    PeerAddress address{.host = target->host(),
                        .port = target->remote_version().listen_port};
    send_peer_list(target.get(), address);
  }
}

void Node::send_peer_list(Peer *peer, const PeerAddress &peer_addr) {
  bool peer_is_local = !AddressBook::is_routable(peer_addr);
  auto gossip = address_book_.reachable(peer_is_local);
  std::erase_if(gossip, [&](const PeerAddress &a) {
    return a.host == peer_addr.host && a.port == peer_addr.port;
  });
  if (gossip.empty())
    return;
  send_msg(peer, MessageType::PEERS, serialize_peer_list(gossip));
}


void Node::request_sync() {
        std::vector<std::shared_ptr<Peer>> targets;
        {
            std::lock_guard<std::mutex> peers_lock(peers_mutex_);


            for(const auto& entry: peers_) {
                if(entry.peer->is_alive()) targets.push_back(entry.peer);
            }
        }

        if(targets.empty()) return;

        auto target = targets[sync_.sync_cursor++ % targets.size()];
        send_msg(target.get(), MessageType::GETBLOCKS, serialize_getblocks(chain_.chain_height()));

}
bool Node::wait_or_stop(std::chrono::milliseconds duration) {
    std::unique_lock<std::mutex> lock(shutdown_mutex_);
    return !shutdown_cv_.wait_for(lock, duration, [this] {return !running_;});
}
void Node::stop() {
{
    std::lock_guard<std::mutex> lock(shutdown_mutex_);
      running_.store(false);
}
  shutdown_cv_.notify_all();
  listener_.close_socket();
  std::vector<std::thread> workers_to_join;
  {

    std::lock_guard<std::mutex> lock(peers_mutex_);
    stopping_.store(true);

    for (auto &peer_entry : peers_) {
      peer_entry.peer->socket().close_socket();
    }

    for (auto &peer_entry : peers_) {
      if (peer_entry.worker.joinable()) {
        workers_to_join.push_back(std::move(peer_entry.worker));
      }
    }
    peers_.clear();
  }

  for (auto &worker : workers_to_join) {
    worker.join();
  }
}
Node::~Node() {
  stop();
  if (connect_thread_.joinable()) {
    connect_thread_.join();
  }
  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }
  if (cleaner_thread_.joinable()) {
    cleaner_thread_.join();
  }
  if (ping_thread_.joinable()) {
    ping_thread_.join();
  }
}
} // namespace forgechain::network
