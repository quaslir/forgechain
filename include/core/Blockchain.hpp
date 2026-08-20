#pragma once

#include "core/Block.hpp"
#include "crypto/CommonTypes.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>
namespace forgechain::core {
using forgechain::core::Block;
struct ForkChain;
enum class BlockValidation : uint8_t { Valid, Invalid, ForkCandidate };
class Blockchain {
public:
  Blockchain();

    void add_block(Block &&block);
    [[nodiscard]] const Block &latest() const;
    [[nodiscard]] const Block &at(size_t height) const;
    [[nodiscard]] bool has_block(const crypto::HashBytes &hash) const;
    [[nodiscard]] size_t size() const;
    [[nodiscard]] std::optional<Block> find(const crypto::HashBytes &hash) const;
    [[nodiscard]] std::optional<size_t> find_height(const crypto::HashBytes& hash) const;
    [[nodiscard]] bool is_valid() const;
    [[nodiscard]] bool empty() const;
    [[nodiscard]] const Block &operator[](size_t height) const;
    [[nodiscard]] BlockValidation
    classify_new_block(const core::Block &block) const;
    [[nodiscard]] std::optional<std::vector<Block>> reorganize_to(ForkChain && fork);
private:
  std::vector<Block> blocks_;
};
} // namespace forgechain::core
