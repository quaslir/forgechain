#include "core/Blockchain.hpp"
#include "crypto/CommonTypes.hpp"
#include "core/Block.hpp"
#include <cstddef>
namespace forgechain::core {
    using forgechain::core::Block;
    using forgechain::crypto::HashBytes;
    Blockchain::Blockchain() {
        Block genesis{1, HashBytes{},0};
        blocks_.push_back(genesis);
    }

    void Blockchain::add_block(const Block& block) {
        blocks_.push_back(block);
    }

    const Block& Blockchain::at(size_t height) const {
        return blocks_.at(height);
    }

    const Block& Blockchain::latest() const {
        return blocks_.back();
    }

    size_t Blockchain::size() const {
        return blocks_.size();
    }

    [[nodiscard]] bool Blockchain::is_valid() const {
        for(size_t height = 1; height < blocks_.size(); height++) {
            const auto& prev_block = blocks_[height - 1];
            const auto& current_block = blocks_[height];
            if(current_block.compute_hash() != current_block.hash_) return false;
            if(prev_block.hash_ != current_block.prev_hash_) return false;
        }

        return true;
    }
    [[nodiscard]] bool Blockchain::empty() const{
        return blocks_.size() == 0;
    }
    [[nodiscard]] const Block& Blockchain::operator[](size_t height) const{
        return blocks_[height];
    }
}
