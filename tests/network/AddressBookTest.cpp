#include "network/AddressBook.hpp"
#include "network/PeerAddress.hpp"
#include <gtest/gtest.h>
#include <string>
#include <thread>
#include <vector>
#include <cstddef>
#include <cstdint>
using forgechain::network::AddressBook;
using forgechain::network::PeerAddress;

namespace {

PeerAddress addr(const std::string &host, uint16_t port = 8000) {
  return PeerAddress{.host = host, .port = port};
}

void fill(AddressBook &book, size_t count) {
  for (size_t i = 0; i < count; ++i) {
    book.add(addr("8.8.8.8", static_cast<uint16_t>(i + 1)));
  }
}

} // namespace


TEST(AddressBookRoutable, AcceptsPublicAddresses) {
  EXPECT_TRUE(AddressBook::is_routable(addr("13.51.48.228")));
  EXPECT_TRUE(AddressBook::is_routable(addr("164.90.194.86")));
  EXPECT_TRUE(AddressBook::is_routable(addr("8.8.8.8")));
  EXPECT_TRUE(AddressBook::is_routable(addr("1.1.1.1")));
  EXPECT_TRUE(AddressBook::is_routable(addr("223.255.255.255")));
}

TEST(AddressBookRoutable, RejectsLoopbackAndThisNetwork) {
  EXPECT_FALSE(AddressBook::is_routable(addr("127.0.0.1")));
  EXPECT_FALSE(AddressBook::is_routable(addr("127.255.255.254")));
  EXPECT_FALSE(AddressBook::is_routable(addr("0.0.0.0")));
  EXPECT_FALSE(AddressBook::is_routable(addr("0.1.2.3")));
}

TEST(AddressBookRoutable, RejectsRfc1918) {
  EXPECT_FALSE(AddressBook::is_routable(addr("10.0.0.1")));
  EXPECT_FALSE(AddressBook::is_routable(addr("10.255.255.255")));
  EXPECT_FALSE(AddressBook::is_routable(addr("192.168.1.1")));
  EXPECT_FALSE(AddressBook::is_routable(addr("172.16.0.1")));
  EXPECT_FALSE(AddressBook::is_routable(addr("172.31.255.255")));
}

TEST(AddressBookRoutable, RespectsRfc1918BoundariesFor172) {
  EXPECT_TRUE(AddressBook::is_routable(addr("172.15.255.255")));
  EXPECT_TRUE(AddressBook::is_routable(addr("172.32.0.1")));
  EXPECT_TRUE(AddressBook::is_routable(addr("172.0.0.1")));
}

TEST(AddressBookRoutable, RespectsBoundariesFor192) {
  EXPECT_TRUE(AddressBook::is_routable(addr("192.167.1.1")));
  EXPECT_TRUE(AddressBook::is_routable(addr("192.169.1.1")));
}

TEST(AddressBookRoutable, RejectsLinkLocal) {
  EXPECT_FALSE(AddressBook::is_routable(addr("169.254.0.1")));
  EXPECT_FALSE(AddressBook::is_routable(addr("169.254.255.255")));
  EXPECT_TRUE(AddressBook::is_routable(addr("169.253.0.1")));
  EXPECT_TRUE(AddressBook::is_routable(addr("169.255.0.1")));
}

TEST(AddressBookRoutable, RejectsMulticastAndReserved) {
  EXPECT_FALSE(AddressBook::is_routable(addr("224.0.0.1")));
  EXPECT_FALSE(AddressBook::is_routable(addr("239.255.255.255")));
  EXPECT_FALSE(AddressBook::is_routable(addr("240.0.0.1")));
  EXPECT_FALSE(AddressBook::is_routable(addr("255.255.255.255")));
}

TEST(AddressBookRoutable, RejectsZeroPortAndEmptyHost) {
  EXPECT_FALSE(AddressBook::is_routable(addr("8.8.8.8", 0)));
  EXPECT_FALSE(AddressBook::is_routable(addr("", 8000)));
}
TEST(AddressBookRoutable, RejectsMalformedInput) {
  EXPECT_FALSE(AddressBook::is_routable(addr("localhost")));
  EXPECT_FALSE(AddressBook::is_routable(addr("example.com")));
  EXPECT_FALSE(AddressBook::is_routable(addr("abc")));
  EXPECT_FALSE(AddressBook::is_routable(addr("999.1.1.1")));
  EXPECT_FALSE(AddressBook::is_routable(addr("1.2.3")));
  EXPECT_FALSE(AddressBook::is_routable(addr("1.2.3.4.5")));
  EXPECT_FALSE(AddressBook::is_routable(addr("1.2.3.-4")));
  EXPECT_FALSE(AddressBook::is_routable(addr(" 8.8.8.8")));
  EXPECT_FALSE(AddressBook::is_routable(addr("::1")));
  EXPECT_FALSE(AddressBook::is_routable(addr("8.8.8.8:80")));
}

TEST(AddressBookRoutable, RejectsLeadingZeroOctets) {
  EXPECT_FALSE(AddressBook::is_routable(addr("010.0.0.1")));
  EXPECT_FALSE(AddressBook::is_routable(addr("127.00.0.1")));
}

TEST(AddressBookAdd, StoresRoutableAddress) {
  AddressBook book;
  EXPECT_TRUE(book.add(addr("13.51.48.228")));
  EXPECT_EQ(book.size(), 1u);
}

TEST(AddressBookAdd, RejectsDuplicate) {
  AddressBook book;
  ASSERT_TRUE(book.add(addr("13.51.48.228")));
  EXPECT_FALSE(book.add(addr("13.51.48.228")));
  EXPECT_EQ(book.size(), 1u);
}

TEST(AddressBookAdd, SameHostDifferentPortsAreDistinct) {
  AddressBook book;
  EXPECT_TRUE(book.add(addr("13.51.48.228", 8000)));
  EXPECT_TRUE(book.add(addr("13.51.48.228", 8001)));
  EXPECT_EQ(book.size(), 2u);
}

TEST(AddressBookAdd, RejectsUnroutableAddress) {
  AddressBook book;
  EXPECT_FALSE(book.add(addr("192.168.1.5")));
  EXPECT_FALSE(book.add(addr("127.0.0.1")));
  EXPECT_FALSE(book.add(addr("8.8.8.8", 0)));
  EXPECT_EQ(book.size(), 0u);
}

TEST(AddressBookAdd, EnforcesCapacityLimit) {
  AddressBook book;
  fill(book, AddressBook::MAX_ENTRIES);
  EXPECT_EQ(book.size(), AddressBook::MAX_ENTRIES);

  EXPECT_FALSE(book.add(addr("9.9.9.9", 1234)));
  EXPECT_EQ(book.size(), AddressBook::MAX_ENTRIES);
}


TEST(AddressBookSelect, EmptyBookYieldsNothing) {
  AddressBook book;
  EXPECT_FALSE(book.select_candidate().has_value());
}

TEST(AddressBookSelect, ReturnsAddedAddress) {
  AddressBook book;
  ASSERT_TRUE(book.add(addr("13.51.48.228", 8000)));

  auto candidate = book.select_candidate();
  ASSERT_TRUE(candidate.has_value());
  EXPECT_EQ(candidate->host, "13.51.48.228");
  EXPECT_EQ(candidate->port, 8000);
}

TEST(AddressBookSelect, DoesNotHandOutSameAddressTwice) {
  AddressBook book;
  ASSERT_TRUE(book.add(addr("13.51.48.228")));

  ASSERT_TRUE(book.select_candidate().has_value());
  EXPECT_FALSE(book.select_candidate().has_value());
}

TEST(AddressBookSelect, EachAddressHandedOutOncePerRound) {
  AddressBook book;
  ASSERT_TRUE(book.add(addr("13.51.48.228", 8000)));
  ASSERT_TRUE(book.add(addr("164.90.194.86", 8000)));

  auto first = book.select_candidate();
  auto second = book.select_candidate();
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(second.has_value());
  EXPECT_NE(first->host, second->host);

  EXPECT_FALSE(book.select_candidate().has_value());
}

TEST(AddressBookSelect, AvailableAgainAfterSuccess) {
  AddressBook book;
  auto address = addr("13.51.48.228");
  ASSERT_TRUE(book.add(address));

  ASSERT_TRUE(book.select_candidate().has_value());
  book.mark_success(address);

  EXPECT_TRUE(book.select_candidate().has_value());
}

TEST(AddressBookSelect, NotAvailableImmediatelyAfterFailure) {
  AddressBook book;
  auto address = addr("13.51.48.228");
  ASSERT_TRUE(book.add(address));

  ASSERT_TRUE(book.select_candidate().has_value());
  book.mark_failure(address);

  EXPECT_FALSE(book.select_candidate().has_value());
}

TEST(AddressBookSelect, AddressIsAbandonedAfterMaxFailures) {
  AddressBook book;
  auto address = addr("13.51.48.228");
  ASSERT_TRUE(book.add(address));

  for (int i = 0; i < AddressBook::MAX_FAILURES; ++i) {
    book.mark_failure(address);
  }

  EXPECT_FALSE(book.select_candidate().has_value());
}

TEST(AddressBookSelect, SkipsAbandonedAddressAndReturnsNext) {
  AddressBook book;
  auto dead = addr("13.51.48.228", 8000);
  auto live = addr("164.90.194.86", 8000);
  ASSERT_TRUE(book.add(dead));
  ASSERT_TRUE(book.add(live));

  for (int i = 0; i < AddressBook::MAX_FAILURES; ++i) {
    book.mark_failure(dead);
  }

  auto candidate = book.select_candidate();
  ASSERT_TRUE(candidate.has_value());
  EXPECT_EQ(candidate->host, live.host);
}

TEST(AddressBookSelect, MarkOnUnknownAddressIsHarmless) {
  AddressBook book;
  book.mark_success(addr("13.51.48.228"));
  book.mark_failure(addr("164.90.194.86"));
  EXPECT_EQ(book.size(), 0u);
}


TEST(AddressBookReachable, EmptyByDefault) {
  AddressBook book;
  EXPECT_TRUE(book.reachable().empty());
}

TEST(AddressBookReachable, ExcludesUnverifiedAddresses) {
  AddressBook book;
  ASSERT_TRUE(book.add(addr("13.51.48.228")));
  EXPECT_TRUE(book.reachable().empty());
}

TEST(AddressBookReachable, IncludesAddressAfterSuccess) {
  AddressBook book;
  auto address = addr("13.51.48.228", 8000);
  ASSERT_TRUE(book.add(address));
  book.mark_success(address);

  auto reachable = book.reachable();
  ASSERT_EQ(reachable.size(), 1u);
  EXPECT_EQ(reachable[0].host, address.host);
  EXPECT_EQ(reachable[0].port, address.port);
}

TEST(AddressBookReachable, ExcludesFailedAddress) {
  AddressBook book;
  auto address = addr("13.51.48.228");
  ASSERT_TRUE(book.add(address));
  book.mark_failure(address);

  EXPECT_TRUE(book.reachable().empty());
}

TEST(AddressBookReachable, StaysVerifiedAfterLaterFailure) {
  AddressBook book;
  auto address = addr("13.51.48.228");
  ASSERT_TRUE(book.add(address));
  book.mark_success(address);
  book.mark_failure(address);

  EXPECT_EQ(book.reachable().size(), 1u);
}

TEST(AddressBookReachable, ReportsOnlyVerifiedSubset) {
  AddressBook book;
  auto verified = addr("13.51.48.228", 8000);
  ASSERT_TRUE(book.add(verified));
  ASSERT_TRUE(book.add(addr("164.90.194.86", 8000)));
  ASSERT_TRUE(book.add(addr("8.8.8.8", 8000)));
  book.mark_success(verified);

  auto reachable = book.reachable();
  ASSERT_EQ(reachable.size(), 1u);
  EXPECT_EQ(reachable[0].host, verified.host);
}

TEST(AddressBookConcurrency, SurvivesParallelAddAndSelect) {
  AddressBook book;
  constexpr int kWriters = 4;
  constexpr int kPerWriter = 100;

  std::vector<std::thread> threads;
  threads.reserve(kWriters + 2);

  for (int w = 0; w < kWriters; ++w) {
    threads.emplace_back([&book, w] {
      for (int i = 0; i < kPerWriter; ++i) {
        book.add(addr("8.8.8." + std::to_string(w + 1),
                      static_cast<uint16_t>(i + 1)));
      }
    });
  }

  threads.emplace_back([&book] {
    for (int i = 0; i < 200; ++i) {
      auto candidate = book.select_candidate();
      if (candidate.has_value()) {
        book.mark_success(*candidate);
      }
    }
  });

  threads.emplace_back([&book] {
    for (int i = 0; i < 200; ++i) {
      (void)book.reachable();
      (void)book.size();
    }
  });

  for (auto &thread : threads) {
    thread.join();
  }

  EXPECT_EQ(book.size(), static_cast<size_t>(kWriters * kPerWriter));
}

TEST(AddressBookConcurrency, NeverHandsSameAddressToTwoThreads) {
  AddressBook book;
  constexpr int kAddresses = 200;
  fill(book, kAddresses);

  std::vector<PeerAddress> first;
  std::vector<PeerAddress> second;

  std::thread t1([&book, &first] {
    for (int i = 0; i < kAddresses; ++i) {
      if (auto candidate = book.select_candidate()) {
        first.push_back(*candidate);
      }
    }
  });
  std::thread t2([&book, &second] {
    for (int i = 0; i < kAddresses; ++i) {
      if (auto candidate = book.select_candidate()) {
        second.push_back(*candidate);
      }
    }
  });
  t1.join();
  t2.join();

  EXPECT_EQ(first.size() + second.size(), static_cast<size_t>(kAddresses));

  std::vector<uint16_t> ports;
  for (const auto &address : first) {
    ports.push_back(address.port);
  }
  for (const auto &address : second) {
    ports.push_back(address.port);
  }
  std::sort(ports.begin(), ports.end());
  EXPECT_EQ(std::adjacent_find(ports.begin(), ports.end()), ports.end());
}
