#include "app/Orchestrator.hpp"
#include "app/ParseNumber.hpp"
#include "consensus/ProofOfWork.hpp"
#include "core/Block.hpp"
#include "core/Mempool.hpp"
#include "core/Transaction.hpp"
#include "crypto/CommonTypes.hpp"
#include "network/Handshake.hpp"
#include "network/Node.hpp"
#include "network/NodeId.hpp"
#include "storage/Storage.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
namespace forgechain::app {

Orchestrator::Orchestrator(OrchestratorConfig config)
    : config_(std::move(config)), log_(config_.node_name),
      storage_(config_.db_path.empty() ? std::nullopt
                                       : std::optional<storage::Storage>(
                                             std::in_place, config_.db_path)),
      chain_manager_(config_.kMaxPending, config_.consensus,
                     storage_.has_value() ? &*storage_ : nullptr),
      node_(config_.listen_port,
            network::VersionInfo{.protocol_version = 1,
                                 .chain_height = 0,
                                 .timestamp = 0,
                                 .listen_port = config_.listen_port,
                                 .node_id = network::generate_node_id()},
            chain_manager_) {
  if (storage_.has_value()) {
    size_t size = storage_->block_count();
    for (size_t i = 1; i < size; i++) {
      auto block = storage_->load_block(i);
      if (!block.has_value()) {
        throw std::runtime_error("storage corrupted: missing block at height " +
                                 std::to_string(i));
      }
      if (!chain_manager_.restore_block(std::move(*block))) {
        throw std::runtime_error("storage corrupted: block at height " +
                                 std::to_string(i) + " does not apply cleanly");
      }
    }
  }

  node_.set_logger(
      [this](const crypto::str &category, const crypto::str &message) {
        log_.log(category, message);
      });
}

bool Orchestrator::start() {
  if (!node_.start())
    return false;
  running_.store(true);
  if (config_.mine) {
    mining_thread_ = std::thread(&Orchestrator::mining_loop, this);
  }
  if (config_.rpc_port > 0) {
    rpc_server_.emplace(node_, config_.rpc_port, chain_manager_);
    if (!rpc_server_->start()) {
      log_.log("RPC", "FAILED to start RPC server on port " +
                          std::to_string(config_.rpc_port));
      rpc_server_.reset();
    } else {
      if (!config_.api_key.empty()) {
        rpc_server_->set_api_key(std::move(config_.api_key));
      }
    }
  }

  for (const auto &address : config_.addresses) {
    node_.remember_peer(address.host, address.port);
  }

  if (!config_.addresses.empty()) {
    log_.log("PEER", "bootstrapping from " +
                         std::to_string(config_.addresses.size()) +
                         " configured peer(s)");
  }

  return true;
}
void Orchestrator::mining_loop() {
  while (running_.load()) {

    std::unique_lock<std::mutex> state_lock(state_mutex_);
    auto tmpl = chain_manager_.block_template(config_.kMaxTxsPerBlock);

    if (!config_.reward_address.empty()) {
      uint64_t fees_total = 0;
      for (const auto &tx : tmpl.transactions) {
        fees_total += tx.fee_;
      }
      core::Transaction coinbase{core::kCoinbaseSender, config_.reward_address,
                                 consensus::mining_reward + fees_total,
                                 crypto::bytes{}, 0};
      tmpl.transactions.insert(tmpl.transactions.begin(), coinbase);
    }
    state_lock.unlock();
    core::Block mined = consensus::mine_block(
        1, tmpl.prev_hash, tmpl.timestamp, tmpl.difficulty, tmpl.transactions);
    node_.submit_block(mined);

    if (chain_manager_.has_block(mined.hash_)) {
      log_.log("MINE",
               "block ACCEPTED, height now " + std::to_string(tmpl.height + 1));
    } else {
      log_.log("MINE", "block REJECTED (stale or invalid)");
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }
}

void Orchestrator::run_command_loop() {
  for (;;) {

    log_.prompt(">>> ");
    crypto::str buffer{};
    if (!std::getline(std::cin, buffer)) {
      break;
    }
    std::istringstream iss(buffer);
    crypto::str command{};
    iss >> command;
    if (command == "balance") {
      crypto::str address{};
      iss >> address;
      if (address.empty())
        continue;
      std::lock_guard<std::mutex> state_lock(state_mutex_);
      handle_balance_command(address);
    } else if (command == "height") {
      std::lock_guard<std::mutex> state_lock(state_mutex_);
      handle_height_command();
    }

    else if (command == "peers") {
      handle_peers_command();
    } else if (command == "status") {
      handle_status_command();
    } else if (command == "addrbook") {
      handle_addrbook_command();
    } else if (command == "set") {
      crypto::str subcommand{};
      iss >> subcommand;
      if (subcommand == "reward-address") {
        crypto::str address{};
        iss >> address;
        if (address.empty()) {
          std::cerr << "usage: set reward-address <address>" << std::endl;
          continue;
        }
        handle_set_reward_address(address);
      } else if (subcommand == "secret-key") {
        crypto::str key{};
        iss >> key;
        if (key.empty()) {
          std::cerr << "usage: set secret-key <value>" << std::endl;
          continue;
        }

        handle_set_secret_key_command(std::move(key));
      } else {
        std::cerr
            << "unknown set target: " << subcommand
            << " (usage: set reward-address <address> | set secret-key <value>)"
            << std::endl;
      }
    } else if (command == "connect") {
      crypto::str host{}, port{};
      if (!(iss >> host >> port)) {
        std::cerr << "usage: connect <host> <port>" << std::endl;
        continue;
      }

      auto port_number = app::parse_number(port);

      if (!port_number.has_value()) {
        std::cerr << "invalid port: " << port << std::endl;
        continue;
      }

      handle_connect_to_peer(host, static_cast<uint16_t>(*port_number));
    } else if (command == "mempool") {
      handle_mempool_command();
    } else if (command == "ledger") {
      handle_ledger_command();
    } else if (command == "help") {
      handle_help_command();
    } else if (command == "quit" || command == "exit") {
      break;
    } else {
      std::cout << "unknown command" << std::endl;
    }
  }
}

void Orchestrator::handle_balance_command(const crypto::str &address) {
  auto balance = chain_manager_.get_balance(address);
  if (!balance.has_value()) {
    std::cout << "unknown address" << std::endl;
    return;
  }

  std::cout << *balance << std::endl;
}
void Orchestrator::handle_height_command() {
  std::cout << chain_manager_.chain_height() << std::endl;
}
void Orchestrator::handle_peers_command() {
  auto peers = node_.peers();
  if (peers.empty()) {
    std::cout << "(no peers connected)" << std::endl;
    return;
  }
  for (const auto &peer : peers) {
    std::cout << peer << std::endl;
  }
}
void Orchestrator::handle_status_command() {
  std::cout << "port: " << config_.listen_port << std::endl;

  if (config_.rpc_port > 0) {
    std::cout << "rpc: active on port " << config_.rpc_port << std::endl;
  } else {
    std::cout << "rpc: disabled" << std::endl;
  }

  if (config_.mine) {
    std::cout << "mining: active" << std::endl;
    if (!config_.reward_address.empty()) {
      std::cout << "reward address: " << config_.reward_address << std::endl;
    } else {
      std::cout << "reward address: none (mined blocks have no coinbase)"
                << std::endl;
    }
  } else {
    std::cout << "mining: disabled" << std::endl;
  }

  std::cout << "difficulty: " << chain_manager_.next_block_difficulty()
            << " bits (next block)" << std::endl;
}

void Orchestrator::handle_set_reward_address(const crypto::str &address) {
  std::lock_guard<std::mutex> state_lock(state_mutex_);
  config_.reward_address = address;
  std::cout << "reward address set to " << address << std::endl;
}
void Orchestrator::handle_help_command() {
  std::cout << "commands:" << std::endl;
  std::cout << "  balance <address>         show Ledger balance for address"
            << std::endl;
  std::cout << "  height                    show current chain height"
            << std::endl;
  std::cout << "  peers                     list connected peers (host:port, "
               "direction)"
            << std::endl;
  std::cout
      << "  addrbook                  list known peer addresses and their state"
      << std::endl;
  std::cout
      << "  mempool                   show pending transactions in mempool"
      << std::endl;
  std::cout
      << "  ledger                    list known addresses and their balances"
      << std::endl;
  std::cout << "  status                    show node configuration"
            << std::endl;
  std::cout << "  set reward-address <addr> change mining reward address"
            << std::endl;
  std::cout << "  set secret-key <value>    set/change the RPC auth token"
            << std::endl;

  std::cout << "  connect <host> <port>     connect to a peer" << std::endl;
  std::cout << "  help                      show this message" << std::endl;
  std::cout << "  quit / exit               shut down the node" << std::endl;
}

void Orchestrator::handle_connect_to_peer(const crypto::str &host,
                                          uint16_t port) {
  if (node_.connect_to_peer(host, port)) {
    std::cout << "connected to " << host << ":" << port << std::endl;
  } else {
    std::cout << "failed to connect to " << host << ":" << port << std::endl;
  }
}
void Orchestrator::handle_mempool_command() {
  auto txs = chain_manager_.mempool_snapshot();
  if (txs.empty()) {
    std::cout << "(mempool is empty)" << std::endl;
    return;
  }
  std::cout << txs.size() << " transaction(s):" << std::endl;
  for (const auto &tx : txs) {
    std::cout << "  " << tx.sender_ << " -> " << tx.recipient_ << " : "
              << tx.amount_ << " (fee: " << tx.fee_ << ")" << std::endl;
  }
}

void Orchestrator::handle_set_secret_key_command(crypto::str &&key) {
  if (!rpc_server_.has_value()) {
    std::cerr << "RPC is not enabled on this node (no --rpc-port given)"
              << std::endl;
    return;
  }
  rpc_server_->set_api_key(std::move(key));
}

void Orchestrator::handle_addrbook_command() {
  auto addrbook = node_.book();

  if (addrbook.empty()) {
    std::cout << "(address book is empty)" << std::endl;
    return;
  }

  for (const auto &info : addrbook) {
    std::cout << info << std::endl;
  }
}

void Orchestrator::handle_ledger_command() {
  auto balances = chain_manager_.all_balances();

  if (balances.empty()) {
    std::cout << "(ledger is empty)" << std::endl;
    return;
  }

  std::cout << balances.size() << " account(s):" << std::endl;
  for (const auto &[address, amount] : balances) {
    std::cout << "  " << address << " : " << amount << std::endl;
  }
}

void Orchestrator::stop() {
  running_.store(false);
  if (mining_thread_.joinable()) {
    mining_thread_.join();
  }
  if (rpc_server_.has_value()) {
    rpc_server_->stop();
  }
  node_.stop();
}
Orchestrator::~Orchestrator() { stop(); }

} // namespace forgechain::app
