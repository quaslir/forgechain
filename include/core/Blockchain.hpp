#pragma once

#include <cstddef>
#include <vector>

#include "core/Block.hpp"
namespace forgechain::core {
using forgechain::core::Block;
class Blockchain {
    public:
        Blockchain();

        void add_block(const Block& block);

       [[nodiscard]] const Block& latest() const;
       [[nodiscard]] const Block& at(size_t height) const;

       [[nodiscard]] size_t size() const;

    private:
        std::vector<Block> blocks_;
};
}
