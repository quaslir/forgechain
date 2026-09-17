// A locator is the list of block hashes a node offers a peer so the peer can
// find where their two chains last agreed. It is dense near the tip, where
// forks actually happen, and sparse further back, where they don't.

#define private public
#include "core/Blockchain.hpp"
#undef private

#include "core/BlockLocator.hpp"
#include "core/Block.hpp"
#include "core/Transaction.hpp"
#include "crypto/CommonTypes.hpp"

#include <gtest/gtest.h>
#include <optional>
#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <vector>

using namespace forgechain::core;
using forgechain::crypto::HashBytes;

namespace {

Blockchain make_chain(size_t count) {
  Blockchain chain;
  for (size_t h = 1; h < count; h++) {
    Block block{1, chain.latest().hash_, 1700000000 + h, {}};
    block.hash_ = block.compute_hash();
    chain.add_block(std::move(block));
  }
  return chain;
}

std::vector<HashBytes> hashes_at(const Blockchain &chain,
                                 const std::vector<size_t> &heights) {
  std::vector<HashBytes> out;
  out.reserve(heights.size());
  for (size_t h : heights) {
    out.push_back(chain.at(h).hash_);
  }
  return out;
}

} // namespace

TEST(BlockLocator, GenesisOnlyChainYieldsOneHash) {
  Blockchain chain;
  auto locator = build_locator(chain);

  ASSERT_EQ(locator.size(), 1u);
  EXPECT_EQ(locator[0], chain.at(0).hash_);
}

TEST(BlockLocator, ShortChainListsEveryBlockNewestFirst) {
  Blockchain chain = make_chain(3);

  EXPECT_EQ(build_locator(chain), hashes_at(chain, {2, 1, 0}));
}

TEST(BlockLocator, FirstTenAreConsecutiveFromTheTip) {
  Blockchain chain = make_chain(100);
  auto locator = build_locator(chain);

  ASSERT_GE(locator.size(), 10u);
  std::vector<HashBytes> head(locator.begin(), locator.begin() + 10);
  EXPECT_EQ(head, hashes_at(chain, {99, 98, 97, 96, 95, 94, 93, 92, 91, 90}));
}

TEST(BlockLocator, StepDoublesAfterTheFirstTen) {
  Blockchain chain = make_chain(100);
  EXPECT_EQ(build_locator(chain),
            hashes_at(chain, {99, 98, 97, 96, 95, 94, 93, 92, 91, 90, 88, 84,
                              76, 60, 28, 0}));
}

TEST(BlockLocator, GenesisIsAlwaysLast) {
  for (size_t count : {size_t{1}, size_t{2}, size_t{3}, size_t{11},
                       size_t{100}, size_t{5000}}) {
    Blockchain chain = make_chain(count);
    auto locator = build_locator(chain);

    ASSERT_FALSE(locator.empty()) << "chain of " << count;
    EXPECT_EQ(locator.back(), chain.at(0).hash_) << "chain of " << count;
  }
}

TEST(BlockLocator, StartsAtTheTip) {
  Blockchain chain = make_chain(500);

  EXPECT_EQ(build_locator(chain).front(), chain.latest().hash_);
}

TEST(BlockLocator, HasNoDuplicates) {
  for (size_t count : {size_t{1}, size_t{2}, size_t{11}, size_t{100}}) {
    auto locator = build_locator(make_chain(count));
    std::unordered_set<HashBytes, forgechain::crypto::HashBytesHasher> unique(
        locator.begin(), locator.end());

    EXPECT_EQ(unique.size(), locator.size()) << "chain of " << count;
  }
}

TEST(BlockLocator, GrowsLogarithmically) {
  EXPECT_LT(build_locator(make_chain(5000)).size(), 30u);
  EXPECT_LT(build_locator(make_chain(50000)).size(), 40u);
}

TEST(BlockLocator, EveryHashBelongsToTheChain) {
  Blockchain chain = make_chain(300);

  for (const auto &hash : build_locator(chain)) {
    EXPECT_TRUE(chain.has_block(hash));
  }
}

TEST(BlockLocator, EmptyChainYieldsNothing) {
  Blockchain chain;
  chain.blocks_.clear();

  EXPECT_TRUE(build_locator(chain).empty());
}

namespace {

void extend(Blockchain &chain, size_t count, uint64_t salt) {
  for (size_t i = 0; i < count; i++) {
    Block block{1, chain.latest().hash_, 1700000000 + salt * 1000 + i, {}};
    chain.add_block(std::move(block));
  }
}

} // namespace

TEST(LocatorMatch, IdenticalChainsMatchAtTheTip) {
  Blockchain chain = make_chain(100);

  EXPECT_EQ(find_locator_match(chain, build_locator(chain)), 99u);
}

TEST(LocatorMatch, PeerBehindMatchesAtItsOwnTip) {
  Blockchain behind = make_chain(50);
  Blockchain ahead = make_chain(100);
  ASSERT_EQ(behind.latest().hash_, ahead.at(49).hash_);

  EXPECT_EQ(find_locator_match(ahead, build_locator(behind)), 49u);
}

TEST(LocatorMatch, MatchIsApproximateOnceTheLocatorGoesSparse) {
  Blockchain behind = make_chain(50);
  Blockchain ahead = make_chain(100);

  auto match = find_locator_match(behind, build_locator(ahead));

  ASSERT_TRUE(match.has_value());
  EXPECT_LE(*match, 49u);
  EXPECT_EQ(behind.at(*match).hash_, ahead.at(*match).hash_);
}

TEST(LocatorMatch, DivergedChainsMatchOnTheSharedPrefix) {
  Blockchain ours = make_chain(100);
  Blockchain theirs = make_chain(100);
  extend(ours, 50, 1);
  extend(theirs, 50, 2);
  ASSERT_NE(ours.latest().hash_, theirs.latest().hash_);

  auto match = find_locator_match(ours, build_locator(theirs));

  ASSERT_TRUE(match.has_value());
  EXPECT_LE(*match, 99u) << "matched inside the diverged part";
  EXPECT_EQ(ours.at(*match).hash_, theirs.at(*match).hash_)
      << "the matched block must be one both chains actually share";
}

TEST(LocatorMatch, GenesisIsTheLastResort) {
  Blockchain ours;
  Blockchain theirs;
  extend(ours, 40, 1);
  extend(theirs, 40, 2);

  EXPECT_EQ(find_locator_match(ours, build_locator(theirs)), 0u);
}

TEST(LocatorMatch, ForeignHashesMatchNothing) {
  Blockchain ours = make_chain(20);
  Blockchain stranger = make_chain(20);
  extend(stranger, 5, 9);
  std::vector<HashBytes> foreign;
  for (size_t h = 20; h < stranger.size(); h++) {
    foreign.push_back(stranger.at(h).hash_);
  }

  EXPECT_EQ(find_locator_match(ours, foreign), std::nullopt);
}

TEST(LocatorMatch, EmptyLocatorMatchesNothing) {
  Blockchain chain = make_chain(20);

  EXPECT_EQ(find_locator_match(chain, {}), std::nullopt);
}

TEST(LocatorMatch, TakesTheFirstMatchNotTheBest) {
  Blockchain chain = make_chain(100);
  std::vector<HashBytes> out_of_order{chain.at(10).hash_, chain.at(90).hash_};

  EXPECT_EQ(find_locator_match(chain, out_of_order), 10u);
}
