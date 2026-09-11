#include "chain/ChainManager.hpp"
#include "consensus/ConsensusParams.hpp"
#include "consensus/ProofOfWork.hpp"
#include "core/Block.hpp"
#include "core/Blockchain.hpp"
#include "core/ForkResolution.hpp"
#include "core/Ledger.hpp"
#include "core/Mempool.hpp"
#include "core/Transaction.hpp"
#include "crypto/CommonTypes.hpp"
#include "storage/Storage.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <functional>
#include <mutex>
#include <optional>
#include <stack>
#include <unordered_set>
#include <utility>
#include <vector>
namespace forgechain::chain {
ChainManager::ChainManager(size_t mempool_max_size,
                           consensus::ConsensusParams params,
                           storage::Storage *storage, Clock clock)
    : mempool_(mempool_max_size), storage_(storage), params_(params),
      clock_(std::move(clock)) {

  if (!clock_) {
    clock_ = []() -> uint64_t {
      return static_cast<uint64_t>(std::time(nullptr));
    };
  }
  if (storage_ && storage_->block_count() == 0) {
    storage_->save_block(blockchain_[0], 0);
  }

  block_at_callback_ = [this](size_t index) -> const core::Block & {
    return blockchain_.at(index);
  };
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
  case core::BlockValidation::Valid: {
    if (!consensus::timestamp_is_valid(block, blockchain_.size(), clock_(),
                                       params_, block_at_callback_)) {
      return outcome;
    }

    uint32_t expected_difficulty = consensus::next_difficulty(
        blockchain_.size(), params_, block_at_callback_);
    if (block.difficulty_ != expected_difficulty) {
      return outcome;
    }

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
  }
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
std::vector<core::Block> ChainManager::blocks_from(size_t from,
                                                   size_t limit) const {
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
  return select_transactions(limit);
}

std::vector<core::Transaction>
ChainManager::select_transactions(size_t limit) const {
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
uint32_t ChainManager::next_block_difficulty() const {
  std::lock_guard<std::mutex> chain_lock(chain_mutex_);
  return consensus::next_difficulty(blockchain_.size(), params_,
                                    block_at_callback_);
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
    auto now = clock_();
    auto tips = find_fork_tips(block);
    std::optional<core::ForkChain> best;
    uint64_t best_work = 0;

    for (const auto &tip : tips) {
      auto result = core::build_fork_chain(blockchain_, orphan_pool_, tip);
      if (result == std::nullopt)
        continue;
      if (!fork_is_valid(*result, now))
        continue;
      uint64_t work = core::fork_work(*result);

      if (!best.has_value() || work > best_work) {
        best = std::move(*result);
        best_work = work;
      }
    }

    if (!best.has_value())
      return outcome;

    auto reorg_hashes = try_reorg(std::move(*best));

    if (reorg_hashes.has_value()) {
      outcome.status = BlockOutcome::Status::Reorged;
      outcome.to_broadcast = std::move(*reorg_hashes);

      for (const auto &hash : outcome.to_broadcast) {
        orphan_pool_.remove_orphan(hash);
      }
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

std::vector<core::Block>
ChainManager::find_fork_tips(const core::Block &start) const {
  std::stack<core::Block> stack;
  std::vector<core::Block> tips;
  size_t visited = 0;
  stack.push(start);

  while (!stack.empty() && visited < core::kMaxForkDepth) {
    core::Block block = std::move(stack.top());
    stack.pop();
    visited++;
    auto result = orphan_pool_.children_of(block.hash_);
    if (result.empty())
      tips.push_back(std::move(block));
    else {
      for (auto &&child : result) {
        stack.push(std::move(child));
      }
    }
  }

  while (!stack.empty()) {
    tips.push_back(std::move(stack.top()));
    stack.pop();
  }
  return tips;
}
bool ChainManager::fork_is_valid(const core::ForkChain &fork,
                                 uint64_t now) const {
  auto height = blockchain_.find_height(fork.common_ancestor.hash_);
  if (!height.has_value())
    return false;
  size_t base = *height + 1;
  auto fork_block_at = [&](size_t index) -> const core::Block & {
    return base > index ? blockchain_.at(index) : fork.blocks.at(index - base);
  };

  for (size_t i = 0; i < fork.blocks.size(); i++) {
    const auto &block = fork.blocks[i];
    if (!consensus::timestamp_is_valid(block, base + i, now, params_,
                                       fork_block_at))
      return false;
    if (block.difficulty_ !=
        consensus::next_difficulty(base + i, params_, fork_block_at))
      return false;
  }

  return true;
}

BlockTemplate ChainManager::block_template(size_t max_txs) const {
  std::lock_guard<std::mutex> chain_lock(chain_mutex_);
  BlockTemplate tmpl;
  tmpl.prev_hash = blockchain_.latest().hash_;
  tmpl.height = blockchain_.size();
  tmpl.difficulty =
      consensus::next_difficulty(tmpl.height, params_, block_at_callback_);
  tmpl.timestamp = std::max(
      clock_(), consensus::median_time_past(tmpl.height, params_.mtp_window,
                                            block_at_callback_) +
                    1);

  tmpl.transactions = select_transactions(max_txs);

  return tmpl;
}
} // namespace forgechain::chain
