#include "storage/SqliteStatement.hpp"
#include "storage/SqliteConnection.hpp"

#include <gtest/gtest.h>
#include <cstdint>
#include <string>
#include <vector>

using namespace forgechain::storage;

namespace {

void exec(SqliteConnection &conn, const std::string &sql) {
  SqliteStatement stmt(conn, sql);
  ASSERT_TRUE(stmt.is_valid()) << "failed to prepare: " << sql;
  ASSERT_EQ(stmt.step(), SQLITE_DONE);
}

SqliteConnection make_test_db() {
  SqliteConnection conn(":memory:");
  exec(conn, "CREATE TABLE t (a INTEGER, b BLOB, c TEXT)");
  return conn;
}

}  // namespace

TEST(SqliteStatement, ValidSqlPreparesSuccessfully) {
  SqliteConnection conn(":memory:");
  SqliteStatement stmt(conn, "SELECT 1");
  EXPECT_TRUE(stmt.is_valid());
}

TEST(SqliteStatement, InvalidSqlFailsToPrepare) {
  SqliteConnection conn(":memory:");
  SqliteStatement stmt(conn, "THIS IS NOT VALID SQL AT ALL");
  EXPECT_FALSE(stmt.is_valid());
}

TEST(SqliteStatement, EmptySqlFailsToPrepare) {
  SqliteConnection conn(":memory:");
  SqliteStatement stmt(conn, "");
  EXPECT_FALSE(stmt.is_valid());
}

TEST(SqliteStatement, InsertThenSelectRoundTripsInt64) {
  SqliteConnection conn = make_test_db();

  {
    SqliteStatement insert(conn, "INSERT INTO t (a) VALUES (?)");
    ASSERT_TRUE(insert.is_valid());
    insert.bind_int_64(1, 424242);
    EXPECT_EQ(insert.step(), SQLITE_DONE);
  }

  SqliteStatement select(conn, "SELECT a FROM t");
  ASSERT_TRUE(select.is_valid());
  ASSERT_EQ(select.step(), SQLITE_ROW);
  EXPECT_EQ(select.column_int64(0), 424242);
  EXPECT_EQ(select.step(), SQLITE_DONE);
}

TEST(SqliteStatement, InsertThenSelectRoundTripsNegativeInt64) {
  SqliteConnection conn = make_test_db();

  {
    SqliteStatement insert(conn, "INSERT INTO t (a) VALUES (?)");
    insert.bind_int_64(1, -123456789);
    ASSERT_EQ(insert.step(), SQLITE_DONE);
  }

  SqliteStatement select(conn, "SELECT a FROM t");
  ASSERT_EQ(select.step(), SQLITE_ROW);
  EXPECT_EQ(select.column_int64(0), -123456789);
}

TEST(SqliteStatement, InsertThenSelectRoundTripsBlobExactly) {
  SqliteConnection conn = make_test_db();
  std::vector<uint8_t> original{0x00, 0xFF, 0x01, 0xFE, 0xAB, 0xCD, 0x00, 0x00};

  {
    SqliteStatement insert(conn, "INSERT INTO t (b) VALUES (?)");
    insert.bind_blob(1, original.data(), original.size());
    ASSERT_EQ(insert.step(), SQLITE_DONE);
  }

  SqliteStatement select(conn, "SELECT b FROM t");
  ASSERT_EQ(select.step(), SQLITE_ROW);
  EXPECT_EQ(select.column_blob(0), original)
      << "blob round trip must preserve embedded zero bytes exactly, not "
         "truncate at the first \\0 the way a naive text-based approach "
         "would";
}

TEST(SqliteStatement, InsertThenSelectRoundTripsEmptyBlob) {
  SqliteConnection conn = make_test_db();
  std::vector<uint8_t> empty{};

  {
    SqliteStatement insert(conn, "INSERT INTO t (b) VALUES (?)");
    insert.bind_blob(1, empty.data(), empty.size());
    ASSERT_EQ(insert.step(), SQLITE_DONE);
  }

  SqliteStatement select(conn, "SELECT b FROM t");
  ASSERT_EQ(select.step(), SQLITE_ROW);
  EXPECT_TRUE(select.column_blob(0).empty());
}

TEST(SqliteStatement, InsertThenSelectRoundTripsText) {
  SqliteConnection conn = make_test_db();

  {
    SqliteStatement insert(conn, "INSERT INTO t (c) VALUES (?)");
    insert.bind_text(1, "some-address-1234567890");
    ASSERT_EQ(insert.step(), SQLITE_DONE);
  }

  SqliteStatement select(conn, "SELECT c FROM t");
  ASSERT_EQ(select.step(), SQLITE_ROW);
  EXPECT_EQ(select.column_text(0), "some-address-1234567890");
}

TEST(SqliteStatement, InsertThenSelectRoundTripsEmptyText) {
  SqliteConnection conn = make_test_db();

  {
    SqliteStatement insert(conn, "INSERT INTO t (c) VALUES (?)");
    insert.bind_text(1, "");
    ASSERT_EQ(insert.step(), SQLITE_DONE);
  }

  SqliteStatement select(conn, "SELECT c FROM t");
  ASSERT_EQ(select.step(), SQLITE_ROW);
  EXPECT_EQ(select.column_text(0), "");
}

TEST(SqliteStatement, AllThreeColumnTypesRoundTripTogetherInOneRow) {
  SqliteConnection conn = make_test_db();
  std::vector<uint8_t> blob_data{1, 2, 3, 4, 5};

  {
    SqliteStatement insert(conn, "INSERT INTO t (a, b, c) VALUES (?, ?, ?)");
    insert.bind_int_64(1, 99);
    insert.bind_blob(2, blob_data.data(), blob_data.size());
    insert.bind_text(3, "hello");
    ASSERT_EQ(insert.step(), SQLITE_DONE);
  }

  SqliteStatement select(conn, "SELECT a, b, c FROM t");
  ASSERT_EQ(select.step(), SQLITE_ROW);
  EXPECT_EQ(select.column_int64(0), 99);
  EXPECT_EQ(select.column_blob(1), blob_data);
  EXPECT_EQ(select.column_text(2), "hello");
}

TEST(SqliteStatement, MultipleRowsAreVisitedInOrderViaRepeatedStep) {
  SqliteConnection conn = make_test_db();
  for (int64_t i = 1; i <= 3; ++i) {
    SqliteStatement insert(conn, "INSERT INTO t (a) VALUES (?)");
    insert.bind_int_64(1, i);
    ASSERT_EQ(insert.step(), SQLITE_DONE);
  }

  SqliteStatement select(conn, "SELECT a FROM t ORDER BY a ASC");
  ASSERT_EQ(select.step(), SQLITE_ROW);
  EXPECT_EQ(select.column_int64(0), 1);
  ASSERT_EQ(select.step(), SQLITE_ROW);
  EXPECT_EQ(select.column_int64(0), 2);
  ASSERT_EQ(select.step(), SQLITE_ROW);
  EXPECT_EQ(select.column_int64(0), 3);
  EXPECT_EQ(select.step(), SQLITE_DONE);
}

TEST(SqliteStatement, SelectOnEmptyTableReturnsDoneImmediately) {
  SqliteConnection conn = make_test_db();
  SqliteStatement select(conn, "SELECT a FROM t");
  EXPECT_EQ(select.step(), SQLITE_DONE);
}

TEST(SqliteStatement, MoveConstructorTransfersOwnership) {
  SqliteConnection conn = make_test_db();
  SqliteStatement original(conn, "SELECT 1");
  ASSERT_TRUE(original.is_valid());

  SqliteStatement moved(std::move(original));
  EXPECT_TRUE(moved.is_valid());
  EXPECT_EQ(moved.step(), SQLITE_ROW);
}

TEST(SqliteStatement, MoveConstructorLeavesSourceInvalid) {
  SqliteConnection conn = make_test_db();
  SqliteStatement original(conn, "SELECT 1");
  ASSERT_TRUE(original.is_valid());

  SqliteStatement moved(std::move(original));
  (void)moved;

  EXPECT_FALSE(original.is_valid());
}

TEST(SqliteStatement, MoveAssignmentClosesPreviousStatement) {
  SqliteConnection conn = make_test_db();
  SqliteStatement a(conn, "SELECT 1");
  SqliteStatement b(conn, "SELECT 2");
  ASSERT_TRUE(a.is_valid());
  ASSERT_TRUE(b.is_valid());

  b = std::move(a);

  EXPECT_TRUE(b.is_valid());
  EXPECT_FALSE(a.is_valid());
}

TEST(SqliteStatement, DestructorDoesNotCrashOnValidStatement) {
  SqliteConnection conn = make_test_db();
  { SqliteStatement stmt(conn, "SELECT 1"); }
  SUCCEED();
}

TEST(SqliteStatement, DestructorDoesNotCrashOnInvalidStatement) {
  SqliteConnection conn(":memory:");
  { SqliteStatement stmt(conn, "NOT VALID SQL"); }
  SUCCEED();
}

TEST(SqliteStatement, BindIndexingIsOneBasedNotZeroBased) {
  SqliteConnection conn = make_test_db();
  SqliteStatement insert(conn, "INSERT INTO t (a, c) VALUES (?, ?)");
  insert.bind_int_64(1, 7);
  insert.bind_text(2, "seven");
  ASSERT_EQ(insert.step(), SQLITE_DONE);

  SqliteStatement select(conn, "SELECT a, c FROM t");
  ASSERT_EQ(select.step(), SQLITE_ROW);
  EXPECT_EQ(select.column_int64(0), 7);
  EXPECT_EQ(select.column_text(1), "seven");
}
