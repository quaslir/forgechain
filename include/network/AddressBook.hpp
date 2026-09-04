#pragma once

#include "network/PeerAddress.hpp"
#include <chrono>
#include <cstddef>
#include <mutex>
#include <optional>
#include <vector>
namespace forgechain::network {

class AddressBook {
public:
  struct AddressEntry {
    PeerAddress address;
    std::chrono::steady_clock::time_point next_try{
        std::chrono::steady_clock::now()};
    int failures{0};
    bool succeeded{false};
    bool in_progress{false};
  };
  static constexpr size_t MAX_ENTRIES{1000};
  static constexpr int MAX_FAILURES{10};
  static constexpr auto BASE_BACKOFF = std::chrono::seconds(5);
  static constexpr auto MAX_BACKOFF = std::chrono::seconds(600);

  bool add(const PeerAddress &peer_address, bool trusted = false);
  std::optional<PeerAddress> select_candidate();
  void mark_success(const PeerAddress &peer_address);
  void mark_failure(const PeerAddress &peer_address);
  void defer(const PeerAddress& peer_address);
  [[nodiscard]] std::vector<PeerAddress>
  reachable(bool include_local = false) const;
  static bool is_routable(const PeerAddress &peer_address);
  [[nodiscard]] size_t size() const;

private:
  AddressEntry *
  find(const PeerAddress &peer_address); // caller must hold book_mutex_
  std::vector<AddressEntry> book_;
  mutable std::mutex book_mutex_;
};
} // namespace forgechain::network
