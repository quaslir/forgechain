#pragma once
#include "sqlite3.h"
#include "storage/SqliteConnection.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace forgechain::storage {
class SqliteStatement {
public:
  explicit SqliteStatement(SqliteConnection &connection,
                           const std::string &sql);
  ~SqliteStatement();

  SqliteStatement(const SqliteStatement &) = delete;
  SqliteStatement &operator=(const SqliteStatement &) = delete;

  SqliteStatement(SqliteStatement &&) noexcept;
  SqliteStatement &operator=(SqliteStatement &&) noexcept;

  [[nodiscard]] bool is_valid() const;

  void bind_int_64(int index, int64_t value);
  void bind_blob(int index, const void *data, size_t size);
  void bind_text(int index, const std::string &value);

  [[nodiscard]] int step();

  [[nodiscard]] int64_t column_int64(int index) const;
  [[nodiscard]] std::vector<uint8_t> column_blob(int index) const;
  [[nodiscard]] std::string column_text(int index) const;

private:
  void close();

  sqlite3_stmt *stmt_{nullptr};
};
} // namespace forgechain::storage
