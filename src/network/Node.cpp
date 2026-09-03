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
#include "network/AddressBook.hpp"
#include "network/GetBlocks.hpp"
#include "network/Handshake.hpp"
#include "network/Inventory.hpp"
#include "network/Message.hpp"
#include "network/Peer.hpp"
#include "network/PeerAddress.hpp"
#include "network/TcpSocket.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>
namespace forgechain::network {
Node::Node(uint16_t listen_port, VersionInfo info, core::Blockchain &blockchain,
           core::Mempool &mempool, core::OrphanPool &orphan_pool,
           core::Ledger &ledger)
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

      handle_peers(msg.payload);
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

void Node::connect_loop() {
while(running_)  {
    while(outbound_peer_count() < TARGET_OUTBOUND_PEERS) {
        auto candidate = address_book_.select_candidate();
        if(!candidate.has_value()) break;
        if(already_connected(candidate->host, candidate->port)) {
            address_book_.mark_success(*candidate);
            continue;
        }
       TcpSocket socket =  connect_to(candidate->host, candidate->port);
       if(!socket.is_valid()) {
            address_book_.mark_failure(*candidate);
            if (logger_) {
              logger_("PEER", "dial failed " + candidate->host + ":" +
                                  std::to_string(candidate->port));
            }
           continue;
       }
       bool res = register_new_peer(std::move(socket), candidate->host, true);
       if(res) {
           address_book_.mark_success(*candidate);
           if (logger_) {
             logger_("PEER", "connected to " + candidate->host + ":" +
                                 std::to_string(candidate->port));
           }
       } else {
           address_book_.mark_failure(*candidate);
           if (logger_) {
             logger_("PEER", "dial failed " + candidate->host + ":" +
                                 std::to_string(candidate->port));
           }
       }
    }

    std::this_thread::sleep_for(CONNECT_INTERVAL);
}
}

bool Node::register_new_peer(TcpSocket &&socket, const crypto::str &host,
                             bool is_outbound) {
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
  if(info_.node_id == incoming_info->node_id) return false;
  auto peer = std::make_shared<Peer>(std::move(socket), *incoming_info, host);
  Peer *raw_peer = peer.get();

  // sync between two peers
  if (my_height < incoming_info->chain_height) {
    if (!send_msg(raw_peer, MessageType::GETBLOCKS,
                  serialize_getblocks(my_height)))
      return false;
  }
  std::vector<PeerAddress> new_peer_address{
      PeerAddress{.host = host, .port = incoming_info->listen_port}};
  crypto::bytes payload = serialize_peer_list(new_peer_address);
  std::vector<std::shared_ptr<Peer>> to_send;
  bool accepted{true};
  {
    std::lock_guard<std::mutex> peers_lock(peers_mutex_);
    for (size_t i = 0; i < peers_.size(); i++) {
        if(!peers_[i].peer->is_alive() || peers_[i].peer->remote_version().listen_port == 0) continue;
        if(peers_[i].peer->host() != host || peers_[i].peer->remote_version().listen_port != incoming_info->listen_port) continue;
      bool keep_new = (info_.node_id < incoming_info->node_id) == is_outbound;
      if(!keep_new) {
          accepted = false;
      }
      else {
          peers_[i].peer->mark_dead();
          peers_[i].peer->socket().close_socket();
      }
      break;
    }

    if (accepted) {
      for (const auto &my_peer : peers_) {
          if(!my_peer.peer->is_alive()) continue;
        to_send.push_back(my_peer.peer);
      }
      peers_.push_back(
          PeerEntry{.peer = peer, .worker = std::thread{&Node::peer_loop, this, peer}, .is_outbound = is_outbound});
    }
  }
  if (!accepted) {
      if (logger_) {
          logger_("PEER", "duplicate connection dropped " + host);
        }
    return false;
  }
  if(is_outbound) {
      address_book_.add(PeerAddress{.host = host, .port = incoming_info->listen_port});
      if (logger_ && !is_outbound) {
        logger_("PEER", "inbound peer " + host + ":" +
                            std::to_string(incoming_info->listen_port));
      }
  }
  send_msg(raw_peer, MessageType::PEERS, serialize_peer_list(address_book_.reachable()));
  if(is_outbound) {
      for (const auto &peer_to_send : to_send) {
        send_msg(peer_to_send.get(), MessageType::PEERS, payload);
      }
  }

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

  if(already_connected(host, port)) return true;
  address_book_.add(PeerAddress{.host = host, .port = port});
  TcpSocket socket = connect_to(host, port);

  return register_new_peer(std::move(socket), host, true);
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
      } else {
        auto block_container_in_orphan = orphan_pool_.find_orphan(item.hash);
        if (block_container_in_orphan.has_value()) {
          send_msg(peer, MessageType::BLOCK,
                   block_container_in_orphan->serialize());
        }
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
  if (!consensus::validate_coinbase_amount(block_container->transactions_))
    return;
  core::BlockValidation block_status;
  {
    std::lock_guard<std::mutex> lock(chain_mutex_);
    block_status = blockchain_.classify_new_block(*block_container);
    if (block_status == core::BlockValidation::Valid) {
      if (!apply_block_to_ledger(*block_container)) {
        block_status = core::BlockValidation::Invalid;
      } else {
        for (const auto &tx : block_container->transactions_) {
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
    auto fork_chain =
        core::build_fork_chain(blockchain_, orphan_pool_, fork_candidate_block);
    if (!fork_chain.has_value()) {
      InventoryItem item{.type = InventoryItemType::BLOCK,
                         .hash = fork_candidate_block.prev_hash_};
      std::vector<InventoryItem> items{item};
      if (peer) {
        send_msg(peer, MessageType::GETDATA, serialize_inventory(items));
      }
    } else {
      auto reorg_hashes = try_reorg(std::move(*fork_chain));
      chain_lock.unlock();
      orphan_lock.unlock();

      if (reorg_hashes.has_value()) {
        for (const auto &reorg_hash : *reorg_hashes) {
          broadcast_inv(peer, InventoryItemType::BLOCK, reorg_hash);
        }
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

void Node::handle_peers(const crypto::bytes &payload) {
  auto list = deserialize_peer_list(payload);
  if (!list.has_value())
    return;

  for (const auto &new_peer : *list) {
      address_book_.add(new_peer);
  }
}

std::optional<std::vector<crypto::HashBytes>>
Node::try_reorg(core::ForkChain &&fork_chain) {

  if (!core::is_fork_heavier(blockchain_, fork_chain))
    return std::nullopt;
  std::vector<crypto::HashBytes> new_hashes;
  std::unordered_set<core::HashBytes, crypto::HashBytesHasher>
      new_branch_hashes;
  std::vector<core::Transaction> new_branch_txs;
  for (const auto &block : fork_chain.blocks) {
    new_hashes.push_back(block.hash_);
    for (const auto &tx : block.transactions_) {
      new_branch_hashes.insert(tx.compute_hash());
      new_branch_txs.push_back(tx);
    }
  }

  auto reorganize_result = blockchain_.reorganize_to(std::move(fork_chain));
  if (!reorganize_result.has_value())
    return std::nullopt;

  std::unordered_set<core::HashBytes, crypto::HashBytesHasher> discarded_hashes;

  for (const auto &block : *reorganize_result) {
    for (const auto &tx : block.transactions_) {
      discarded_hashes.insert(tx.compute_hash());
    }
  }

  for (auto it = reorganize_result->rbegin(); it != reorganize_result->rend();
       it++) {
    for (auto tx = it->transactions_.rbegin(); tx != it->transactions_.rend();
         tx++) {
      if (new_branch_hashes.contains(tx->compute_hash()))
        continue;
      ledger_.reverse_transaction(*tx);
      mempool_.add_transaction(*tx, tx->sender_public_key_);
    }
  }

  for (const auto &tx : new_branch_txs) {
    if (discarded_hashes.contains(tx.compute_hash()))
      continue;
    ledger_.apply_transaction(tx);
  }

  return new_hashes;
}

bool Node::apply_block_to_ledger(const core::Block &block) {
  const auto &transactions = block.transactions_;

  for (size_t i = 0; i < transactions.size(); i++) {
    if (!ledger_.apply_transaction(transactions[i])) {
      for (size_t j = i; j > 0; j--) {
        ledger_.reverse_transaction(transactions[j - 1]);
      }
      return false;
    }
  }

  return true;
}

void Node::submit_block(const core::Block &block) {
  handle_block(nullptr, block.serialize());
}
void Node::submit_transaction(const core::Transaction &tx) {
  handle_tx(nullptr, tx.serialize());
}

size_t Node::chain_height() const {
  std::lock_guard<std::mutex> chain_lock(chain_mutex_);
  return blockchain_.size();
}
std::optional<uint64_t> Node::get_balance(const crypto::str &address) const {
  std::lock_guard<std::mutex> chain_lock(chain_mutex_);
  return ledger_.get_balance(address);
}
crypto::HashBytes Node::latest_hash() const {
  std::lock_guard<std::mutex> chain_lock(chain_mutex_);
  return blockchain_.latest().hash_;
}
std::vector<core::Transaction>
Node::transactions_for_block(size_t limit) const {
  std::lock_guard<std::mutex> chain_lock(chain_mutex_);

  auto candidates = mempool_.get_transactions_for_block(limit);

  core::Ledger simulated{ledger_};
  std::vector<core::Transaction> valid;

  for (const auto &tx : candidates) {
    if (simulated.apply_transaction(tx)) {
      valid.push_back(tx);
    }
  }

  return valid;
}

std::vector<core::Transaction> Node::mempool_snapshot() const {
  std::lock_guard<std::mutex> chain_lock(chain_mutex_);
  return mempool_.get_transactions_for_block(mempool_.size());
}

void Node::set_balance(const crypto::str &address, uint64_t amount) {
  std::lock_guard<std::mutex> chain_lock(chain_mutex_);
  ledger_.set_balance(address, amount);
}

void Node::set_logger(
    std::function<void(const crypto::str &, const crypto::str &)> logger) {
  logger_ = std::move(logger);
}


bool Node::already_connected(const crypto::str &host, uint16_t port) const {
std::lock_guard<std::mutex> peers_lock(peers_mutex_);

for(const auto& entry : peers_) {
    if(entry.peer->is_alive() && entry.peer->host() == host && entry.peer->remote_version().listen_port == port) return true;
}

return false;
}
size_t Node::outbound_peer_count() const {
    size_t count{0};
    std::lock_guard<std::mutex> peers_lock(peers_mutex_);

    for(const auto& entry : peers_) {
        if(entry.is_outbound && entry.peer->is_alive()) count++;
    }

    return count;
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
  if(connect_thread_.joinable()) {
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
