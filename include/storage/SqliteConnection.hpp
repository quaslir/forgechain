#pragma once
#include "sqlite3.h"
#include <string>
namespace forgechain::storage {
class SqliteConnection {
public:
  explicit SqliteConnection(const std::string &path);
  ~SqliteConnection();

  SqliteConnection(const SqliteConnection &) = delete;
  SqliteConnection &operator=(const SqliteConnection &) = delete;

  SqliteConnection(SqliteConnection &&) noexcept;
  SqliteConnection &operator=(SqliteConnection &&) noexcept;

  [[nodiscard]] bool is_valid() const;

  [[nodiscard]] sqlite3 *handle() const;

private:
  void close();
  sqlite3 *db_{nullptr};
};
} // namespace forgechain::storage
