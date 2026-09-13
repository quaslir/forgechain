#pragma once
#include "consensus/ConsensusParams.hpp"
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
#include <functional>
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

struct BlockTemplate {
  crypto::HashBytes prev_hash{};
  size_t height{0};
  uint64_t timestamp{0};
  uint32_t difficulty{0};
  std::vector<core::Transaction> transactions{};
};

class ChainManager {
public:
  using Clock = std::function<uint64_t()>;
  explicit ChainManager(size_t mempool_max_size,
                        consensus::ConsensusParams params,
                        storage::Storage *storage = nullptr, Clock clock = {});

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
  [[nodiscard]] std::vector<core::Block> blocks_from(size_t from,
                                                     size_t limit) const;

  [[nodiscard]] bool has_transaction(const crypto::HashBytes &hash) const;
  [[nodiscard]] std::optional<core::Transaction>
  find_transaction(const crypto::HashBytes &hash) const;

  [[nodiscard]] std::vector<core::Transaction> mempool_snapshot() const;

  [[nodiscard]] std::optional<uint64_t>
  get_balance(const crypto::str &address) const;

  [[nodiscard]] core::Block block_at(size_t index) const;
  [[nodiscard]] std::vector<std::pair<crypto::str, uint64_t>>
  all_balances() const;
  [[nodiscard]] uint32_t next_block_difficulty() const;
  [[nodiscard]] BlockTemplate block_template(size_t max_txs) const;
  [[nodiscard]] std::vector<core::Transaction>
  transactions_for_block(size_t limit) const;

private:
  std::vector<core::Transaction> select_transactions(
      size_t limit) const; // MUST be called with chain_mutex_!!!
  [[nodiscard]] bool apply_block_to_ledger(const core::Block &block);
  BlockOutcome handle_fork_candidate(const core::Block &block);
  std::optional<std::vector<crypto::HashBytes>>
  try_reorg(core::ForkChain &&fork_chain);
  [[nodiscard]] std::vector<core::Block> find_fork_tips(
      const core::Block &start) const; // MUST be called with orphan_mutex_ !!!
  [[nodiscard]] size_t valid_prefix_length(const core::ForkChain &fork,
                                   uint64_t now) const;
  core::Blockchain blockchain_;
  core::Mempool mempool_;
  core::OrphanPool orphan_pool_;
  core::Ledger ledger_;
  storage::Storage *storage_;
  consensus::ConsensusParams params_;
  Clock clock_;
  mutable std::mutex chain_mutex_;
  mutable std::mutex orphan_mutex_;

  std::function<const core::Block &(size_t index)>
      block_at_callback_; // MUST be called with chain_mutex_!!!
};
} // namespace forgechain::chain
