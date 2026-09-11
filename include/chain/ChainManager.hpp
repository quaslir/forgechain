#pragma once
#include "core/Block.hpp"
#include "core/Blockchain.hpp"
#include "core/ForkResolution.hpp"
#include "core/Ledger.hpp"
#include "core/Mempool.hpp"
#include "core/OrphanPool.hpp"
#include "core/Transaction.hpp"
#include "crypto/CommonTypes.hpp"
#include "storage/Storage.hpp"
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>
namespace forgechain::chain {

struct BlockOutcome {
  enum class Status : uint8_t { Rejected, Accepted, Reorged, NeedParent };

  Status status{Status::Rejected};
  std::vector<crypto::HashBytes> to_broadcast;
  std::optional<crypto::HashBytes> missing_parent;
};

class ChainManager {
public:
  explicit ChainManager(size_t mempool_max_size,
                        storage::Storage *storage = nullptr);

  ChainManager(const ChainManager &) = delete;
  ChainManager &operator=(const ChainManager &) = delete;

  BlockOutcome submit_block(const core::Block &block);
  bool submit_transaction(const core::Transaction &tx);
  void set_balance(const crypto::str &address, uint64_t amount);
  bool restore_block(core::Block &&block);

  [[nodiscard]] size_t chain_height() const;
  [[nodiscard]] crypto::HashBytes latest_hash() const;
  [[nodiscard]] bool has_block(const crypto::HashBytes &hash) const;
  [[nodiscard]] std::optional<core::Block>
  find_block(const crypto::HashBytes &hash) const;
  [[nodiscard]] std::vector<core::Block> blocks_from(size_t from, size_t limit) const;

  [[nodiscard]] bool has_transaction(const crypto::HashBytes &hash) const;
  [[nodiscard]] std::optional<core::Transaction>
  find_transaction(const crypto::HashBytes &hash) const;
  [[nodiscard]] std::vector<core::Transaction>
  transactions_for_block(size_t limit) const;
  [[nodiscard]] std::vector<core::Transaction> mempool_snapshot() const;

  [[nodiscard]] std::optional<uint64_t>
  get_balance(const crypto::str &address) const;

  [[nodiscard]] core::Block block_at(size_t index) const;
  [[nodiscard]] std::vector<std::pair<crypto::str, uint64_t>>
  all_balances() const;

private:
  [[nodiscard]] bool apply_block_to_ledger(const core::Block &block);
  BlockOutcome handle_fork_candidate(const core::Block &block);
  std::optional<std::vector<crypto::HashBytes>>
  try_reorg(core::ForkChain &&fork_chain);

  core::Blockchain blockchain_;
  core::Mempool mempool_;
  core::OrphanPool orphan_pool_;
  core::Ledger ledger_;
  storage::Storage *storage_;
  mutable std::mutex chain_mutex_;
  mutable std::mutex orphan_mutex_;
};
} // namespace forgechain::chain
