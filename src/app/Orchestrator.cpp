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
#include <ctime>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
namespace forgechain::app {

Orchestrator::Orchestrator(OrchestratorConfig config)
    : config_(std::move(config)), log_(config_.node_name),
      mempool_(config_.kMaxPending),
      node_(config_.listen_port,
            network::VersionInfo{.protocol_version = 1,
                                 .chain_height = 0,
                                 .timestamp = 0,
                                 .listen_port = config_.listen_port,
                                 .node_id = network::generate_node_id()},
            chain_, mempool_, orphan_pool_, ledger_) {
  if (!config_.db_path.empty()) {
    storage_.emplace(config_.db_path);
  }
  if (storage_.has_value()) {
    size_t size = storage_->block_count();
    if (size > 1) {
      for (size_t i = 1; i < size; i++) {
        auto block = storage_->load_block(i);
        if (!block.has_value()) {
          throw std::runtime_error(
              "storage corrupted: missing block at height " +
              std::to_string(i));
        }
        chain_.add_block(std::move(*block));
      }
    }
    auto balances = storage_->load_all_balances();
    for (const auto &[address, amount] : balances) {
      ledger_.set_balance(address, amount);
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
  if (config_.mine_every_seconds > 0) {
    mining_thread_ = std::thread(&Orchestrator::mining_loop, this);
  }
  if (config_.rpc_port > 0) {
    rpc_server_.emplace(node_, config_.rpc_port);
    if (!rpc_server_->start()) {
      log_.log("RPC", "FAILED to start RPC server on port " +
                          std::to_string(config_.rpc_port));
      rpc_server_.reset();
    }
  }
  bool at_least_one_peer_connected{false};
  for (const auto &address : config_.addresses) {
    if (node_.connect_to_peer(address.host, address.port)) {
      at_least_one_peer_connected = true;
    } else {
      log_.log("PEER", "FAILED to connect to " + address.host + ":" +
                           std::to_string(address.port));
    }
  }
  if (!at_least_one_peer_connected) {
    log_.log(
        "PEER",
        "FAILED to connect to any configured peer -- continuing as listener");
  }

  return true;
}
void Orchestrator::mining_loop() {
  while (running_.load()) {
    std::this_thread::sleep_for(
        std::chrono::seconds(config_.mine_every_seconds));
    if (!running_.load())
      break;

    std::lock_guard<std::mutex> state_lock(state_mutex_);
    size_t prev_height = node_.chain_height();
    crypto::HashBytes prev_hash = node_.latest_hash();
    auto txs_for_block = node_.transactions_for_block(config_.kMaxTxsPerBlock);

    if (!config_.reward_address.empty()) {
      uint64_t fees_total = 0;
      for (const auto &tx : txs_for_block) {
        fees_total += tx.fee_;
      }
      core::Transaction coinbase{core::kCoinbaseSender, config_.reward_address,
                                 consensus::mining_reward + fees_total,
                                 crypto::bytes{}, 0};
      txs_for_block.insert(txs_for_block.begin(), coinbase);
    }

    core::Block mined = consensus::mine_block(
        1, prev_hash, static_cast<uint64_t>(std::time(nullptr)),
        config_.mine_difficulty, txs_for_block);
    node_.submit_block(mined);

    if (node_.chain_height() == prev_height) {
      log_.log("MINE", "block REJECTED (height unchanged at " +
                           std::to_string(prev_height) + ")");
    } else {
      log_.log("MINE", "block ACCEPTED, height now " +
                           std::to_string(node_.chain_height()));
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
  auto balance = node_.get_balance(address);
  if (!balance.has_value()) {
    std::cout << "unknown address" << std::endl;
    return;
  }

  std::cout << *balance << std::endl;
}
void Orchestrator::handle_height_command() {
  std::cout << node_.chain_height() << std::endl;
}
void Orchestrator::handle_peers_command() {
  std::cout << node_.peer_count() << std::endl;
}
void Orchestrator::handle_status_command() {
  std::cout << "port: " << config_.listen_port << std::endl;

  if (config_.rpc_port > 0) {
    std::cout << "rpc: active on port " << config_.rpc_port << std::endl;
  } else {
    std::cout << "rpc: disabled" << std::endl;
  }

  if (config_.mine_every_seconds > 0) {
    std::cout << "mining: active, every " << config_.mine_every_seconds << "s"
              << std::endl;

    if (!config_.reward_address.empty()) {
      std::cout << "reward address: " << config_.reward_address << std::endl;
    } else {
      std::cout << "reward address: none (mined blocks have no coinbase)"
                << std::endl;
    }
  } else {
    std::cout << "mining: disabled" << std::endl;
  }
}

void Orchestrator::handle_set_reward_address(const crypto::str &address) {
  std::lock_guard<std::mutex> state_lock(state_mutex_);
  config_.reward_address = address;
  std::cout << "reward address set to " << address << std::endl;
}
void Orchestrator::handle_help_command() {
  std::cout << "commands:" << std::endl;
  std::cout << "  balance <address>        show Ledger balance for address"
            << std::endl;
  std::cout << "  height                    show current chain height"
            << std::endl;
  std::cout << "  peers                     show connected peer count"
            << std::endl;
  std::cout
      << "  mempool                   show pending transactions in mempool"
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
  auto txs = node_.mempool_snapshot();
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

void Orchestrator::stop() {
  running_.store(false);
  if (mining_thread_.joinable()) {
    mining_thread_.join();
  }
  if (rpc_server_.has_value()) {
    rpc_server_->stop();
  }
  node_.stop();

  if (storage_.has_value()) {
    for (size_t i = 0; i < chain_.size(); i++) {
      const auto &block = chain_.at(i);
      storage_->save_block(block, i);
    }
    auto balances = ledger_.all_balances();

    for (const auto &[address, amount] : balances) {
      storage_->save_balance(address, amount);
    }
  }
}
Orchestrator::~Orchestrator() { stop(); }

} // namespace forgechain::app
