#include "storage/Storage.hpp"
#include "storage/SqliteConnection.hpp"
#include "storage/SqliteStatement.hpp"
#include <stdexcept>
#include "core/Block.hpp"
#include <vector>
#include <string>
#include <optional>
#include <cstddef>
#include "crypto/CommonTypes.hpp"
#include <cstdint>
#include <utility>


namespace forgechain::storage {
     Storage::Storage(const std::string& path) : connection_(path) {
         if(!connection_.is_valid()) {
             throw std::runtime_error("failed to open storage database: " + path);
         }
         create_schema();
     }
            void Storage::create_schema() {
                SqliteStatement blocks_statement{connection_, "CREATE TABLE IF NOT EXISTS blocks ("
                    "height INTEGER PRIMARY KEY, "
                    "hash BLOB NOT NULL UNIQUE, "
                    "serialized BLOB NOT NULL)"};
                if(!blocks_statement.is_valid()) {
                    throw std::runtime_error("failed to prepare blocks table schema");
                }

                int blocks_step = blocks_statement.step();

                if(blocks_step != SQLITE_DONE) {
                    throw std::runtime_error("failed to create blocks table");
                }



                SqliteStatement balance_statement{connection_, "CREATE TABLE IF NOT EXISTS balances ("
                    "address TEXT PRIMARY KEY, "
                    "amount INTEGER NOT NULL)"};
                if(!balance_statement.is_valid()) {
                    throw std::runtime_error("failed to prepare balances table schema");
                }

                int balance_step = balance_statement.step();

                if(balance_step != SQLITE_DONE) {
                    throw std::runtime_error("failed to create balances table");
                }


            }
    void Storage::save_block(const core::Block& block, size_t height) {
        SqliteStatement stmt(connection_, "INSERT INTO blocks (height, hash, serialized) VALUES (?, ?, ?)");
        if(!stmt.is_valid()) {
            throw std::runtime_error("failed to prepare INSERT for blocks");
        }

        stmt.bind_int_64(1, static_cast<int64_t>(height));
        stmt.bind_blob(2, block.hash_.data(), block.hash_.size());
        auto serialized = block.serialize();
        stmt.bind_blob(3, serialized.data(), serialized.size());

        if(stmt.step() != SQLITE_DONE) {
            throw std::runtime_error("failed to insert block");
        }
    }
   std::optional<core::Block> Storage::load_block(size_t height) const {
       SqliteStatement stmt(connection_, "SELECT serialized FROM blocks WHERE height = ?");
       if(!stmt.is_valid()) {
           throw std::runtime_error("failed to prepare SELECT for blocks");
       }
       stmt.bind_int_64(1, static_cast<int64_t>(height));
       if(stmt.step() != SQLITE_ROW) {
           return std::nullopt;
       }

       auto raw = stmt.column_blob(0);
      return core::Block::deserialize(raw);
   }
   size_t Storage::block_count() const {
       SqliteStatement stmt(connection_, "SELECT COUNT(*) FROM blocks");
       if(!stmt.is_valid()) {
           throw std::runtime_error("failed to prepare SELECT for block");
       }

       if(stmt.step() != SQLITE_ROW) {
           throw std::runtime_error("failed to count blocks");
       }

       return static_cast<size_t>(stmt.column_int64(0));
   }

   void Storage::save_balance(const crypto::str& address, uint64_t amount) {
       SqliteStatement stmt(connection_, "INSERT OR REPLACE INTO balances (address, amount) VALUES (?, ?)");
       if(!stmt.is_valid()) {
           throw std::runtime_error("failed to prepare INSERT for balances");
       }

       stmt.bind_text(1, address);
       stmt.bind_int_64(2, static_cast<int64_t>(amount));

       if(stmt.step() != SQLITE_DONE) {
           throw std::runtime_error("failed to insert balance");
       }
   }
    std::optional<uint64_t> Storage::load_balance(const crypto::str& address) const {
        SqliteStatement stmt(connection_, "SELECT amount FROM balances WHERE address = ?");
        if(!stmt.is_valid()) {
            throw std::runtime_error("failed to prepare SELECT for balances");
        }
        stmt.bind_text(1, address);
        if(stmt.step() != SQLITE_ROW) {
            return std::nullopt;
        }

        return static_cast<uint64_t>(stmt.column_int64(0));
   }
   std::vector<std::pair<crypto::str, uint64_t>> Storage::load_all_balances() const {
       SqliteStatement stmt(connection_, "SELECT address, amount FROM balances");
       if(!stmt.is_valid()) {
           throw std::runtime_error("failed to prepare SELECT for balances");
       }
       std::vector<std::pair<crypto::str, uint64_t>> balances;

       while(stmt.step() == SQLITE_ROW) {
           crypto::str address = stmt.column_text(0);
           auto amount = static_cast<uint64_t>(stmt.column_int64(1));

           balances.emplace_back(std::move(address), amount);
       }
       return balances;

   }

}
