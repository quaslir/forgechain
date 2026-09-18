#pragma once
#include "core/Block.hpp"
#include "crypto/CommonTypes.hpp"
#include <cstddef>
#include <list>
#include <optional>
#include <unordered_map>
#include <vector>
namespace forgechain::core {
static constexpr auto kDefaultMaxOrphans = 1000;
struct Entry {
  Block block;
  std::list<HashBytes>::iterator pos;
};
class OrphanPool {
public:
  explicit OrphanPool(size_t max_size = kDefaultMaxOrphans);
  void add_orphan(Block &&block);
  [[nodiscard]] bool has_orphan(const crypto::HashBytes &hash) const;
  [[nodiscard]] std::optional<Block>
  find_orphan(const crypto::HashBytes &hash) const;
  [[nodiscard]] std::optional<Block>
  find_orphan_by_prev_hash(const crypto::HashBytes &prev_hash) const;

  void remove_orphan(const crypto::HashBytes &hash);

  [[nodiscard]] size_t orphan_count() const;
  [[nodiscard]] std::vector<Block>
  children_of(const crypto::HashBytes &hash) const;

private:
  std::list<crypto::HashBytes> order_;
  std::unordered_map<crypto::HashBytes, Entry, crypto::HashBytesHasher>
      orphan_pool_;
  size_t max_size_;
};

} // namespace forgechain::core
