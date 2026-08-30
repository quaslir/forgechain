#include "storage/SqliteConnection.hpp"
#include "sqlite3.h"
#include <string>
namespace forgechain::storage {
SqliteConnection::SqliteConnection(const std::string &path) {
  int res = sqlite3_open(path.c_str(), &db_);
  if (res != SQLITE_OK) {
    close();
    return;
  }
}

SqliteConnection::SqliteConnection(SqliteConnection &&other) noexcept {
  db_ = other.db_;
  other.db_ = nullptr;
}
SqliteConnection &
SqliteConnection::operator=(SqliteConnection &&other) noexcept {
  if (this != &other) {
    close();
    db_ = other.db_;
    other.db_ = nullptr;
  }
  return *this;
}

bool SqliteConnection::is_valid() const { return db_ != nullptr; }

sqlite3 *SqliteConnection::handle() const { return db_; }

void SqliteConnection::close() {
  if (db_ != nullptr) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

SqliteConnection::~SqliteConnection() { close(); }
} // namespace forgechain::storage
