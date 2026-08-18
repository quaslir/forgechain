#include "network/Node.hpp"
#include "core/Block.hpp"
#include "core/Blockchain.hpp"
#include "core/Mempool.hpp"
#include "core/Transaction.hpp"
#include "crypto/CommonTypes.hpp"
#include "network/Handshake.hpp"
#include "network/Inventory.hpp"
#include "network/Message.hpp"
#include "network/Peer.hpp"
#include "network/TcpSocket.hpp"
#include "consensus/ProofOfWork.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <sys/socket.h>
#include <thread>
#include <utility>
#include <vector>
namespace forgechain::network {
Node::Node(uint16_t listen_port, VersionInfo info, core::Blockchain &blockchain,
           core::Mempool &mempool)
    : listen_port_(listen_port), info_(info), blockchain_(blockchain),
      mempool_(mempool) {}

bool Node::start() {
  listener_ = listen_on(listen_port_);
  if (!listener_.is_valid())
    return false;
  listener_.set_receive_timeout(1);
  running_.store(true);
  accept_thread_ = std::thread(&Node::accept_loop, this);
  cleaner_thread_ = std::thread(&Node::cleaner_loop, this);
  return true;
}

void Node::accept_loop() {
  while (running_) {
    accept_one_peer();
  }
}
void Node::peer_loop(Peer *peer) {
  while (running_ && peer->is_alive()) {
    Message msg;
    if (!receive_message(peer->socket().fd(), msg)) {
      peer->mark_dead();
      break;
    }

    switch(msg.type) {
        case MessageType::INV :
            handle_inv(peer, msg.payload);
            break;
        case MessageType::GETDATA :
            handle_getdata(peer, msg.payload);
            break;
        case MessageType::BLOCK :
        case MessageType::TX :
            break;
        default:
            break;
    }
  }
}
void Node::cleaner_loop() {
  while (running_) {
    {
      std::lock_guard<std::mutex> lock(peers_mutex_);
      for (auto &peer_entry : peers_) {
        if (!peer_entry.peer->is_alive() && peer_entry.worker.joinable()) {
          peer_entry.worker.join();
        }
      }
      std::erase_if(peers_, [](const PeerEntry &peer_entry) {
        return peer_entry.peer->is_alive() == false;
      });
    }

    std::this_thread::sleep_for(CLEANER_TIMEOUT);
  }
}

bool Node::register_new_peer(TcpSocket &&socket) {
  if (!socket.is_valid())
    return false;
  socket.set_receive_timeout(5);
  auto incoming_info = perform_handshake(socket.fd(), info_);
  if (!incoming_info.has_value())
    return false;
  auto peer = std::make_unique<Peer>(std::move(socket), *incoming_info);
  Peer *raw_peer = peer.get();
  std::thread worker{&Node::peer_loop, this, raw_peer};
  {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    peers_.push_back(
        PeerEntry{.peer = std::move(peer), .worker = std::move(worker)});
  }

  return true;
}

bool Node::accept_one_peer() {
  TcpSocket socket = accept_connection(listener_);
  return register_new_peer(std::move(socket));
}

bool Node::connect_to_peer(const crypto::str &host, uint16_t port) {
  TcpSocket socket = connect_to(host, port);

  return register_new_peer(std::move(socket));
}

[[nodiscard]] size_t Node::peer_count() const {
  std::lock_guard<std::mutex> lock(peers_mutex_);
  return peers_.size();
}
bool Node::send_msg(Peer * peer, MessageType type, const crypto::bytes& payload) {
    Message msg{.type = type, .payload = payload};
    return send_message(peer->socket().fd(), msg);
}

bool Node::is_valid_new_block_unlocked(const core::Block& block) const {

        if(block.prev_hash_ != blockchain_.latest().hash_) {
            return false;
    }

    if(block.hash_ != block.compute_hash()) {
        return false;
    }

    if(!consensus::meets_target(block.hash_, block.difficulty_)) {
        return false;
    }

    return true;
}

void Node::handle_inv(Peer * peer, const crypto::bytes& payload) {
    auto items = deserialize_inventory(payload);
    if(!items.has_value()) return;

    std::vector<InventoryItem> missing;

    for(const auto& item : *items) {
        bool have_it = false;
        if(item.type == InventoryItemType::BLOCK) {
            have_it = blockchain_.has_block(item.hash);
        }
        else if(item.type == InventoryItemType::TRANSACTION) {
            have_it = mempool_.has_transaction(item.hash);
        }

        if(!have_it) {
            missing.push_back(item);
        }
    }

    if(!missing.empty()) {
        crypto::bytes getdata_payload = serialize_inventory(missing);
        send_msg(peer, MessageType::GETDATA, getdata_payload);
    }
}

void Node::handle_getdata(Peer * peer, const crypto::bytes&payload) {

    auto items = deserialize_inventory(payload);
    if(!items.has_value()) return;

    for(const auto& item : *items) {
        if(item.type == InventoryItemType::BLOCK) {
            auto block_container = blockchain_.find(item.hash);
            if(block_container.has_value()) {
                send_msg(peer, MessageType::BLOCK, block_container->serialize());
            }
        }

        else if(item.type == InventoryItemType::TRANSACTION) {
            auto tx_container = mempool_.find(item.hash);
            if(tx_container.has_value()) {
                send_msg(peer, MessageType::TX, tx_container->serialize() );
            }
        }
    }
}
void Node::handle_block(const crypto::bytes& payload) {
    auto block_container = core::Block::deserialize(payload);
    if(!block_container.has_value()) return;
    std::lock_guard<std::mutex> lock(chain_mutex_);
    if(!is_valid_new_block_unlocked(*block_container)) return;
    blockchain_.add_block(*block_container);
}

void Node::handle_tx(const crypto::bytes& payload) {
    auto tx_container = core::Transaction::deserialize(payload);
    if(!tx_container.has_value()) return;
    //mempool_.add_transaction(*tx_container);
}

void Node::stop() {
  running_.store(false);
  listener_.close_socket();
  std::vector<std::thread> workers_to_join;
  {

    std::lock_guard<std::mutex> lock(peers_mutex_);

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
  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }
  if (cleaner_thread_.joinable()) {
    cleaner_thread_.join();
  }
}
} // namespace forgechain::network
