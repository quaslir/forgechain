#include "storage/SqliteConnection.hpp"

#include <gtest/gtest.h>
#include <utility>

using namespace forgechain::storage;

TEST(SqliteConnection, InMemoryDatabaseOpensSuccessfully) {
  SqliteConnection conn(":memory:");
  EXPECT_TRUE(conn.is_valid());
}

TEST(SqliteConnection, HandleReturnsNonNullOnValidConnection) {
  SqliteConnection conn(":memory:");
  ASSERT_TRUE(conn.is_valid());
  EXPECT_NE(conn.handle(), nullptr);
}

TEST(SqliteConnection, OpeningInNonExistentDirectoryFails) {
  SqliteConnection conn("/this/directory/does/not/exist/db.sqlite");
  EXPECT_FALSE(conn.is_valid());
}

TEST(SqliteConnection, MoveConstructorTransfersOwnership) {
  SqliteConnection original(":memory:");
  ASSERT_TRUE(original.is_valid());
  auto *original_handle = original.handle();

  SqliteConnection moved(std::move(original));

  EXPECT_TRUE(moved.is_valid());
  EXPECT_EQ(moved.handle(), original_handle);
}

TEST(SqliteConnection, MoveConstructorLeavesSourceInvalid) {
  SqliteConnection original(":memory:");
  ASSERT_TRUE(original.is_valid());

  SqliteConnection moved(std::move(original));
  (void)moved;

  EXPECT_FALSE(original.is_valid());
  EXPECT_EQ(original.handle(), nullptr);
}

TEST(SqliteConnection, MoveAssignmentTransfersOwnershipAndInvalidatesSource) {
  SqliteConnection a(":memory:");
  SqliteConnection b(":memory:");
  ASSERT_TRUE(a.is_valid());
  ASSERT_TRUE(b.is_valid());
  auto *a_handle = a.handle();

  b = std::move(a);

  EXPECT_EQ(b.handle(), a_handle);
  EXPECT_FALSE(a.is_valid());
}

TEST(SqliteConnection, MoveAssignmentClosesPreviouslyOwnedConnection) {
  SqliteConnection a(":memory:");
  SqliteConnection b(":memory:");
  b = std::move(a);
  SUCCEED();
}

TEST(SqliteConnection, SelfMoveAssignmentDoesNotCrashOrInvalidate) {
  SqliteConnection conn(":memory:");
  ASSERT_TRUE(conn.is_valid());


  SqliteConnection &alias = conn;
  conn = std::move(alias);

  EXPECT_TRUE(conn.is_valid());
}

TEST(SqliteConnection, DestructorDoesNotCrashOnValidConnection) {
  { SqliteConnection conn(":memory:"); }
  SUCCEED();
}

TEST(SqliteConnection, DestructorDoesNotCrashOnInvalidConnection) {
  { SqliteConnection conn("/this/directory/does/not/exist/db.sqlite"); }
  SUCCEED();
}

TEST(SqliteConnection, MultipleInMemoryConnectionsAreIndependent) {
  SqliteConnection a(":memory:");
  SqliteConnection b(":memory:");
  ASSERT_TRUE(a.is_valid());
  ASSERT_TRUE(b.is_valid());
  EXPECT_NE(a.handle(), b.handle());
}
