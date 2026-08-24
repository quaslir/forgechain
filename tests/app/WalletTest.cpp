#include "app/Wallet.hpp"
#include "crypto/Address.hpp"
#include "crypto/CommonTypes.hpp"

#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

using namespace forgechain::app;
using namespace forgechain::crypto;

namespace {

class TempKeyfile {
public:
  explicit TempKeyfile(const std::string &suffix) {
    path_ = (std::filesystem::temp_directory_path() /
             ("forgechain_wallet_test_" + suffix + "_" +
              std::to_string(::getpid())))
                .string();
    std::filesystem::remove(path_);
  }

  ~TempKeyfile() { std::filesystem::remove(path_); }

  TempKeyfile(const TempKeyfile &) = delete;
  TempKeyfile &operator=(const TempKeyfile &) = delete;

  [[nodiscard]] const std::string &path() const { return path_; }

private:
  std::string path_;
};

} // namespace

TEST(Wallet, GeneratesNewKeypairWhenFileDoesNotExist) {
  TempKeyfile keyfile("generates_new");
  ASSERT_FALSE(std::filesystem::exists(keyfile.path()));

  Wallet wallet(keyfile.path());

  EXPECT_TRUE(std::filesystem::exists(keyfile.path()));
  EXPECT_FALSE(wallet.address().empty());
}

TEST(Wallet, GeneratedAddressMatchesDerivedAddressConvention) {
  TempKeyfile keyfile("address_format");
  Wallet wallet(keyfile.path());

  EXPECT_EQ(wallet.address().size(), 64u)
      << "double-SHA256 hex address should be 64 hex characters";
}

TEST(Wallet, TwoFreshWalletsGetDifferentAddresses) {
  TempKeyfile keyfile_a("fresh_a");
  TempKeyfile keyfile_b("fresh_b");

  Wallet wallet_a(keyfile_a.path());
  Wallet wallet_b(keyfile_b.path());

  EXPECT_NE(wallet_a.address(), wallet_b.address());
}

TEST(Wallet, ReloadingSameKeyfileYieldsSameAddress) {
  TempKeyfile keyfile("reload_same");
  str first_address;
  {
    Wallet wallet(keyfile.path());
    first_address = wallet.address();
  }

  Wallet reloaded(keyfile.path());

  EXPECT_EQ(reloaded.address(), first_address);
}

TEST(Wallet, KeyfileContainsTwoHexLines) {
  TempKeyfile keyfile("two_lines");
  { Wallet wallet(keyfile.path()); }

  std::ifstream file(keyfile.path());
  ASSERT_TRUE(file.is_open());
  std::string line1, line2, line3;
  std::getline(file, line1);
  std::getline(file, line2);
  bool has_third = static_cast<bool>(std::getline(file, line3));

  EXPECT_FALSE(line1.empty());
  EXPECT_FALSE(line2.empty());
  EXPECT_FALSE(has_third) << "keyfile should contain exactly two lines";
}

TEST(Wallet, EmptyKeyfileThrows) {
  TempKeyfile keyfile("empty_file");
  { std::ofstream file(keyfile.path()); }

  EXPECT_THROW({ Wallet wallet(keyfile.path()); }, std::runtime_error);
}

TEST(Wallet, KeyfileWithOnlyOneLineThrows) {
  TempKeyfile keyfile("one_line");
  {
    std::ofstream file(keyfile.path());
    file << "deadbeef" << std::endl;
  }

  EXPECT_THROW({ Wallet wallet(keyfile.path()); }, std::runtime_error);
}

TEST(Wallet, KeyfileWithInvalidHexThrows) {
  TempKeyfile keyfile("invalid_hex");
  {
    std::ofstream file(keyfile.path());
    file << "not-valid-hex!!" << std::endl;
    file << "also-not-hex" << std::endl;
  }

  EXPECT_THROW({ Wallet wallet(keyfile.path()); }, std::runtime_error);
}

TEST(Wallet, KeyfileWithOddLengthHexThrows) {
  TempKeyfile keyfile("odd_length");
  {
    std::ofstream file(keyfile.path());
    file << "abc" << std::endl;
    file << "abcdef" << std::endl;
  }

  EXPECT_THROW({ Wallet wallet(keyfile.path()); }, std::runtime_error);
}

TEST(Wallet, DoesNotOverwriteExistingValidKeyfile) {
  TempKeyfile keyfile("no_overwrite");
  str original_address;
  {
    Wallet wallet(keyfile.path());
    original_address = wallet.address();
  }
  auto original_write_time = std::filesystem::last_write_time(keyfile.path());

  Wallet reloaded(keyfile.path());

  EXPECT_EQ(reloaded.address(), original_address);
  EXPECT_EQ(std::filesystem::last_write_time(keyfile.path()),
            original_write_time);
}
