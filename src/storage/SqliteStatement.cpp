#include "storage/SqliteStatement.hpp"
#include "storage/SqliteConnection.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace forgechain::storage {
SqliteStatement::SqliteStatement(SqliteConnection &connection,
                                 const std::string &sql) {
  int res =
      sqlite3_prepare_v2(connection.handle(), sql.c_str(), -1, &stmt_, nullptr);
  if (res != SQLITE_OK) {
    close();
    return;
  }
}

SqliteStatement::SqliteStatement(SqliteStatement &&other) noexcept {
  stmt_ = other.stmt_;
  other.stmt_ = nullptr;
}
SqliteStatement &SqliteStatement::operator=(SqliteStatement &&other) noexcept {
  if (this != &other) {
    close();
    stmt_ = other.stmt_;
    other.stmt_ = nullptr;
  }
  return *this;
}

bool SqliteStatement::is_valid() const { return stmt_ != nullptr; }

void SqliteStatement::bind_int_64(int index, int64_t value) {
  sqlite3_bind_int64(stmt_, index, value);
}
void SqliteStatement::bind_blob(int index, const void *data, size_t size) {
  sqlite3_bind_blob(stmt_, index, data, static_cast<int>(size),
                    SQLITE_TRANSIENT);
}
void SqliteStatement::bind_text(int index, const std::string &value) {
  sqlite3_bind_text(stmt_, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

int SqliteStatement::step() { return sqlite3_step(stmt_); }

int64_t SqliteStatement::column_int64(int index) const {
  return sqlite3_column_int64(stmt_, index);
}
std::vector<uint8_t> SqliteStatement::column_blob(int index) const {
  int size = sqlite3_column_bytes(stmt_, index);
  const auto *bytes =
      static_cast<const uint8_t *>(sqlite3_column_blob(stmt_, index));
  return std::vector<uint8_t>{bytes, bytes + size};
}
std::string SqliteStatement::column_text(int index) const {
  const auto *text = sqlite3_column_text(stmt_, index);
  if (text == nullptr) {
    return {};
  }
  return std::string{reinterpret_cast<const char *>(text)};
}

void SqliteStatement::close() {
  if (stmt_ != nullptr) {
    sqlite3_finalize(stmt_);
    stmt_ = nullptr;
  }
}

SqliteStatement::~SqliteStatement() { close(); }
} // namespace forgechain::storage
