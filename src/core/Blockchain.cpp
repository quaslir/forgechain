#include "core/Blockchain.hpp"
#include "crypto/Hash.hpp"
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
}
