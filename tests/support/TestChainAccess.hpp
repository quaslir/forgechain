#pragma once

// Test-only access to ChainManager's internals.
//
// ChainManager deliberately exposes no handles to the containers it owns --
// that encapsulation is the point of the class. Tests still need to seed and
// inspect raw state, so this header reopens it for them and nowhere else.
//
// Include this INSTEAD OF "chain/ChainManager.hpp" in test files.

#define private public
#include "chain/ChainManager.hpp"
#undef private

#include "core/Blockchain.hpp"
#include "core/Ledger.hpp"
#include "core/Mempool.hpp"
#include "core/OrphanPool.hpp"
#include "network/Handshake.hpp"
#include "network/Node.hpp"
#include <cstddef>
#include <cstdint>

namespace forgechain::testsupport {

inline constexpr size_t kTestMempoolSize = 1000;

struct ChainHolder {
  chain::ChainManager manager{kTestMempoolSize};
};
struct TestNode : ChainHolder, network::Node {
  explicit TestNode(uint16_t listen_port = 0)
      : network::Node(listen_port,
                      network::VersionInfo{.protocol_version = 1,
                                           .chain_height = 0,
                                           .timestamp = 0,
                                           .listen_port = listen_port,
                                           .node_id = 0},
                      manager) {}

  TestNode(uint16_t listen_port, network::VersionInfo info)
      : network::Node(listen_port, info, manager) {}

  [[nodiscard]] chain::ChainManager &chain_manager() { return manager; }

  [[nodiscard]] core::Blockchain &blocks() { return manager.blockchain_; }
  [[nodiscard]] core::Ledger &ledger() { return manager.ledger_; }
  [[nodiscard]] core::Mempool &mempool() { return manager.mempool_; }
  [[nodiscard]] core::OrphanPool &orphans() { return manager.orphan_pool_; }
};

} // namespace forgechain::testsupport
