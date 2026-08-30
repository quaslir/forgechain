#pragma once
#include <string>
#include "sqlite3.h"
namespace forgechain::storage {
    class SqliteConnection {
        public:
            explicit SqliteConnection(const std::string& path);
            ~SqliteConnection();

            SqliteConnection(const SqliteConnection&) = delete;
            SqliteConnection& operator=(const SqliteConnection&) = delete;

            SqliteConnection(SqliteConnection &&) noexcept;
            SqliteConnection& operator=(SqliteConnection &&) noexcept;

            [[nodiscard]] bool is_valid() const;

            [[nodiscard]] sqlite3 * handle() const;
            void close();
        private:
            sqlite3 * db_{nullptr};
    };
}
