#include "app/Orchestrator.hpp"
#include "consensus/ProofOfWork.hpp"
#include "core/Block.hpp"
#include "core/Blockchain.hpp"
#include "core/Transaction.hpp"
#include "crypto/CommonTypes.hpp"
#include "network/Handshake.hpp"
#include "network/Node.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
namespace forgechain::app {

Orchestrator::Orchestrator(OrchestratorConfig config)
    : config_(std::move(config)), log_(config_.node_name),
      node_(config_.listen_port, network::VersionInfo{}, chain_, mempool_,
            orphan_pool_, ledger_) {}

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
      core::Transaction coinbase{core::kCoinbaseSender, config_.reward_address,
                                 OrchestratorConfig::mining_reward,
                                 crypto::bytes{}};
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
    }

    else if (command == "quit" || command == "exit") {
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
