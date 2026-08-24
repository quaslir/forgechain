#pragma once
#include "app/DemoLog.hpp"
#include "app/RpcServer.hpp"
#include "core/Blockchain.hpp"
#include "core/Ledger.hpp"
#include "core/Mempool.hpp"
#include "core/OrphanPool.hpp"
#include "crypto/CommonTypes.hpp"
#include "crypto/Keys.hpp"
#include "network/Node.hpp"
#include "network/PeerAddress.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>
#include <sstream>
namespace forgechain::app {

struct OrchestratorConfig {
  struct Host {
    crypto::str connect_host;
    uint16_t connect_port{0};
  };
  uint16_t listen_port{8000};
  std::vector<network::PeerAddress> addresses;
  // 0 disables mining entirely (pure listener node).
  int mine_every_seconds = 0;
  static constexpr uint32_t mine_difficulty = 15;
  static constexpr size_t kMaxTxsPerBlock = 50;
  uint16_t rpc_port{0};
  static constexpr uint64_t mining_reward = 50;
  crypto::str node_name = "NODE";
  crypto::str reward_address{};
};
class Orchestrator {
public:
  explicit Orchestrator(OrchestratorConfig config);
  ~Orchestrator();
  Orchestrator(const Orchestrator &) = delete;
  Orchestrator &operator=(const Orchestrator &) = delete;

  [[nodiscard]] bool start();
  void stop();
  void run_command_loop();

private:
  void mining_loop();

  void handle_balance_command(const crypto::str &address);
  void handle_height_command();
  void handle_peers_command();
  void handle_set_balance_command(std::istringstream& iss);
  OrchestratorConfig config_;
  demo::DemoLog log_;
  core::Blockchain chain_;
  core::Mempool mempool_;
  core::OrphanPool orphan_pool_;
  core::Ledger ledger_;
  network::Node node_;
  std::optional<RpcServer> rpc_server_;
  mutable std::mutex state_mutex_;

  std::atomic<bool> running_{false};
  std::thread mining_thread_;
};
} // namespace forgechain::app
