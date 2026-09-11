#include "chain/ChainManager.hpp"
#include "consensus/ProofOfWork.hpp"
#include "core/Block.hpp"
#include "core/Blockchain.hpp"
#include "core/ForkResolution.hpp"
#include "core/Ledger.hpp"
#include "core/Mempool.hpp"
#include "core/Transaction.hpp"
#include "crypto/CommonTypes.hpp"
#include "storage/Storage.hpp"
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_set>
#include <utility>
#include <vector>
namespace forgechain::chain {
ChainManager::ChainManager(size_t mempool_max_size, storage::Storage *storage)
    : mempool_(mempool_max_size), storage_(storage) {
  if (storage_ && storage_->block_count() == 0) {
    storage_->save_block(blockchain_[0], 0);
  }
}

BlockOutcome ChainManager::submit_block(const core::Block &block) {
  BlockOutcome outcome;
  if (!consensus::meets_target(block.hash_, block.difficulty_))
    return outcome;
  if (!consensus::validate_coinbase_amount(block.transactions_))
    return outcome;
  std::lock_guard<std::mutex> chain_lock(chain_mutex_);
  core::BlockValidation block_status = blockchain_.classify_new_block(block);

  switch (block_status) {
  case core::BlockValidation::Valid:
    if (apply_block_to_ledger(block)) {
      for (const auto &tx : block.transactions_) {
        mempool_.remove_transaction(tx);
      }
      auto saved{block};
      blockchain_.add_block(std::move(saved));
      outcome.status = BlockOutcome::Status::Accepted;
      outcome.to_broadcast.push_back(block.hash_);

      // save
      if (storage_) {
        storage_->save_block(block, blockchain_.size() - 1);
      }
    }
    break;
  case core::BlockValidation::ForkCandidate:
    return handle_fork_candidate(block);
  case core::BlockValidation::Invalid:
    return outcome;
  }

  return outcome;
}
bool ChainManager::submit_transaction(const core::Transaction &tx) {
  std::lock_guard<std::mutex> chain_lock(chain_mutex_);
  return mempool_.add_transaction(tx, tx.sender_public_key_);
}
void ChainManager::set_balance(const crypto::str &address, uint64_t amount) {
  std::lock_guard<std::mutex> chain_lock(chain_mutex_);
  ledger_.set_balance(address, amount);
}
bool ChainManager::restore_block(core::Block &&block) {
  std::lock_guard<std::mutex> chain_lock(chain_mutex_);
  if (!apply_block_to_ledger(block))
    return false;
  blockchain_.add_block(std::move(block));
  return true;
}

size_t ChainManager::chain_height() const {
  std::lock_guard<std::mutex> chain_lock(chain_mutex_);
  return blockchain_.size();
}
crypto::HashBytes ChainManager::latest_hash() const {
  std::lock_guard<std::mutex> chain_lock(chain_mutex_);
  return blockchain_.latest().hash_;
}
bool ChainManager::has_block(const crypto::HashBytes &hash) const {
  std::lock_guard<std::mutex> chain_lock(chain_mutex_);
  return blockchain_.has_block(hash);
}
std::optional<core::Block>
ChainManager::find_block(const crypto::HashBytes &hash) const {
  {
    std::lock_guard<std::mutex> chain_lock(chain_mutex_);
    auto block = blockchain_.find(hash);
    if (block.has_value())
      return block;
  }

  std::lock_guard<std::mutex> orphan_lock(orphan_mutex_);
  auto block_in_orphan = orphan_pool_.find_orphan(hash);
  return block_in_orphan;
}
std::vector<core::Block> ChainManager::blocks_from(size_t from, size_t limit) const {
  std::lock_guard<std::mutex> chain_lock(chain_mutex_);
  if (from >= blockchain_.size())
    return {};
  std::vector<core::Block> blocks;
  size_t end = std::min(blockchain_.size(), from + limit);
  blocks.reserve(end - from);

  for (size_t i = from; i < end; i++) {
    blocks.push_back(blockchain_[i]);
  }

  return blocks;
}

bool ChainManager::has_transaction(const crypto::HashBytes &hash) const {
  std::lock_guard<std::mutex> chain_lock(chain_mutex_);
  return mempool_.has_transaction(hash);
}
std::optional<core::Transaction>
ChainManager::find_transaction(const crypto::HashBytes &hash) const {
  std::lock_guard<std::mutex> chain_lock(chain_mutex_);
  return mempool_.find(hash);
}
std::vector<core::Transaction>
ChainManager::transactions_for_block(size_t limit) const {
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
std::vector<core::Transaction> ChainManager::mempool_snapshot() const {
  std::lock_guard<std::mutex> chain_lock(chain_mutex_);
  return mempool_.get_transactions_for_block(mempool_.size());
}

std::optional<uint64_t>
ChainManager::get_balance(const crypto::str &address) const {
  std::lock_guard<std::mutex> chain_lock(chain_mutex_);
  return ledger_.get_balance(address);
}

core::Block ChainManager::block_at(size_t index) const {
  std::lock_guard<std::mutex> chain_lock(chain_mutex_);
  return blockchain_.at(index);
}
std::vector<std::pair<crypto::str, uint64_t>>
ChainManager::all_balances() const {
  std::lock_guard<std::mutex> chain_lock(chain_mutex_);
  return ledger_.all_balances();
}

bool ChainManager::apply_block_to_ledger(const core::Block &block) {
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
BlockOutcome ChainManager::handle_fork_candidate(const core::Block &block) {
  BlockOutcome outcome;

  std::lock_guard<std::mutex> orphan_lock(orphan_mutex_);

  orphan_pool_.add_orphan(core::Block(block));
  auto fork_chain = core::build_fork_chain(blockchain_, orphan_pool_, block);
  if (!fork_chain.has_value()) {
    outcome.status = BlockOutcome::Status::NeedParent;
    outcome.missing_parent = block.prev_hash_;
  } else {
    auto reorg_hashes = try_reorg(std::move(*fork_chain));

    if (reorg_hashes.has_value()) {
      outcome.status = BlockOutcome::Status::Reorged;
      outcome.to_broadcast = std::move(*reorg_hashes);
    }
  }

  return outcome;
}
std::optional<std::vector<crypto::HashBytes>>
ChainManager::try_reorg(core::ForkChain &&fork_chain) {
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

  if (storage_) {
    size_t fork_point = blockchain_.size() - new_hashes.size();
    std::vector<core::Block> new_branch;
    new_branch.reserve(new_hashes.size());
    for (size_t i = fork_point; i < blockchain_.size(); i++) {
      new_branch.push_back(blockchain_[i]);
    }

    storage_->replace_blocks_from(fork_point, new_branch);
  }
  return new_hashes;
}
} // namespace forgechain::chain
