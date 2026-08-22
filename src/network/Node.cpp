#include "network/Node.hpp"
#include "consensus/ProofOfWork.hpp"
#include "core/Block.hpp"
#include "core/Blockchain.hpp"
#include "core/ForkResolution.hpp"
#include "core/Ledger.hpp"
#include "core/Mempool.hpp"
#include "core/OrphanPool.hpp"
#include "core/Transaction.hpp"
#include "crypto/CommonTypes.hpp"
#include "network/GetBlocks.hpp"
#include "network/Handshake.hpp"
#include "network/Inventory.hpp"
#include "network/Message.hpp"
#include "network/Peer.hpp"
#include "network/TcpSocket.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_set>
#include <sys/socket.h>
#include <thread>
#include <utility>
#include <vector>
#include <optional>
namespace forgechain::network {
Node::Node(uint16_t listen_port, VersionInfo info, core::Blockchain &blockchain,
           core::Mempool &mempool, core::OrphanPool& orphan_pool, core::Ledger& ledger)
    : listen_port_(listen_port), info_(info), blockchain_(blockchain),
      mempool_(mempool), orphan_pool_(orphan_pool), ledger_(ledger) {}

bool Node::start() {
  listener_ = listen_on(listen_port_);
  if (!listener_.is_valid())
    return false;
  listener_.set_receive_timeout(1);
  running_.store(true);
  accept_thread_ = std::thread(&Node::accept_loop, this);
  cleaner_thread_ = std::thread(&Node::cleaner_loop, this);
  ping_thread_ = std::thread(&Node::ping_loop, this);
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

void Node::ping_loop() {
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

    std::this_thread::sleep_for(PING_INTERVAL);
  }
}

bool Node::register_new_peer(TcpSocket &&socket) {
  if (!socket.is_valid())
    return false;
  socket.set_receive_timeout(5);
  std::unique_lock<std::mutex> chain_lock(chain_mutex_);
  auto my_height = static_cast<uint64_t>(blockchain_.size());
  chain_lock.unlock();
  VersionInfo current_info = info_;
  current_info.chain_height = my_height;

  auto incoming_info = perform_handshake(socket.fd(), current_info);
  if (!incoming_info.has_value())
    return false;
  auto peer = std::make_unique<Peer>(std::move(socket), *incoming_info);
  Peer *raw_peer = peer.get();

  // sync between two peers
  if (my_height < incoming_info->chain_height) {
    if (!send_msg(raw_peer, MessageType::GETBLOCKS,
                  serialize_getblocks(my_height)))
      return false;
  }

  std::thread worker{&Node::peer_loop, this, raw_peer};
  {
    std::lock_guard<std::mutex> peers_lock(peers_mutex_);
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
      have_it = blockchain_.has_block(item.hash);
    } else if (item.type == InventoryItemType::TRANSACTION) {
      have_it = mempool_.has_transaction(item.hash);
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
      auto block_container = blockchain_.find(item.hash);
      if (block_container.has_value()) {
        send_msg(peer, MessageType::BLOCK, block_container->serialize());
      }
    }

    else if (item.type == InventoryItemType::TRANSACTION) {
      auto tx_container = mempool_.find(item.hash);
      if (tx_container.has_value()) {
        send_msg(peer, MessageType::TX, tx_container->serialize());
      }
    }
  }
}
void Node::handle_block(Peer *peer, const crypto::bytes &payload) {
  auto block_container = core::Block::deserialize(payload);
  if (!block_container.has_value())
    return;
  crypto::HashBytes hash = block_container->hash_;
  if (!consensus::meets_target(hash, block_container->difficulty_))
    return;

  core::BlockValidation block_status;
  {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    block_status = blockchain_.classify_new_block(*block_container);
    if (block_status == core::BlockValidation::Valid) {
        if(!apply_block_to_ledger(*block_container)) {
            block_status = core::BlockValidation::Invalid;
        } else {
                for(const auto&tx : block_container->transactions_) {
                    mempool_.remove_transaction(tx);
                }
                blockchain_.add_block(std::move(*block_container));
        }


    }
  }

  switch (block_status) {
  case core::BlockValidation::Valid:
    broadcast_inv(peer, InventoryItemType::BLOCK, hash);
    break;
  case core::BlockValidation::ForkCandidate: {
      core::Block fork_candidate_block{*block_container};

      std::unique_lock<std::mutex> chain_lock(chain_mutex_);
      std::unique_lock<std::mutex> orphan_lock(orphan_mutex_);

      orphan_pool_.add_orphan(std::move(*block_container));
      auto reorg_hashes = try_reorg(fork_candidate_block);
      chain_lock.unlock();
      orphan_lock.unlock();

      if(reorg_hashes.has_value()) {
          for(const auto& reorg_hash : *reorg_hashes) {
              broadcast_inv(peer, InventoryItemType::BLOCK, reorg_hash);
          }
      }
          break;
  }


  case core::BlockValidation::Invalid:
    break;
  }
}

void Node::handle_tx(Peer *peer, const crypto::bytes &payload) {
  auto tx_container = core::Transaction::deserialize(payload);
  if (!tx_container.has_value())
    return;
  if (!mempool_.add_transaction(*tx_container,
                                tx_container->sender_public_key_))
    return;
  broadcast_inv(peer, InventoryItemType::TRANSACTION,
                tx_container->compute_hash());
}

void Node::handle_getblocks(Peer *peer, const crypto::bytes &payload) {
  auto from_height_container = deserialize_getblocks(payload);
  if (!from_height_container.has_value())
    return;

  std::lock_guard<std::mutex> lock(chain_mutex_);

  for (auto i = static_cast<size_t>(*from_height_container);
       i < blockchain_.size(); i++) {
    send_msg(peer, MessageType::BLOCK, blockchain_[i].serialize());
  }
}

void Node::handle_ping(Peer *peer) { send_msg(peer, MessageType::PONG, {}); }
void Node::handle_pong() {}
std::optional<std::vector<crypto::HashBytes>> Node::try_reorg(const core::Block& candidate) {
    auto fork_chain =  core::build_fork_chain(blockchain_, orphan_pool_, candidate);

    if(!fork_chain.has_value()) return std::nullopt;
    if(!core::is_fork_heavier(blockchain_, *fork_chain)) return std::nullopt;
    std::vector<crypto::HashBytes> new_hashes;
    std::unordered_set<core::HashBytes, crypto::HashBytesHasher> new_branch_hashes;
    std::vector<core::Transaction> new_branch_txs;
    for(const auto& block : fork_chain->blocks) {
        new_hashes.push_back(block.hash_);
        for(const auto& tx: block.transactions_) {
            new_branch_hashes.insert(tx.compute_hash());
            new_branch_txs.push_back(tx);
        }
    }

    auto reorganize_result = blockchain_.reorganize_to(std::move(*fork_chain));
    if(!reorganize_result.has_value()) return std::nullopt;

std::unordered_set<core::HashBytes, crypto::HashBytesHasher> discarded_hashes;

for(const auto& block : *reorganize_result) {
    for(const auto& tx : block.transactions_) {
        discarded_hashes.insert(tx.compute_hash());
    }
}

for(auto it = reorganize_result->rbegin(); it != reorganize_result->rend(); it++) {
    for(auto tx = it->transactions_.rbegin(); tx != it->transactions_.rend(); tx++) {
                if(new_branch_hashes.contains(tx->compute_hash())) continue;
        ledger_.reverse_transaction(*tx);
        mempool_.add_transaction(*tx, tx->sender_public_key_);
    }
}

for(const auto& tx: new_branch_txs) {
    if(discarded_hashes.contains(tx.compute_hash())) continue;
    ledger_.apply_transaction(tx);
}

    return new_hashes;
}

bool Node::apply_block_to_ledger(const core::Block& block) {
const auto& transactions = block.transactions_;

for(size_t i = 0; i < transactions.size(); i++) {
if(!ledger_.apply_transaction(transactions[i])) {
    for(size_t j = i; j > 0; j--) {
        ledger_.reverse_transaction(transactions[j - 1]);
    }
    return false;
}
}

return true;
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
  if (ping_thread_.joinable()) {
    ping_thread_.join();
  }
}
} // namespace forgechain::network
