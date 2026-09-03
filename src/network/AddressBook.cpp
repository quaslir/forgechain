#include "network/AddressBook.hpp"
#include "crypto/CommonTypes.hpp"
#include "network/PeerAddress.hpp"
#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <sys/socket.h>
#include <vector>

namespace forgechain::network {
bool AddressBook::is_routable(const PeerAddress &peer_address) {
  const crypto::str &host = peer_address.host;
  if (host.empty() || peer_address.port == 0)
    return false;
  struct in_addr addr{};
  int res = inet_pton(AF_INET, host.c_str(), &addr);
  if (res != 1)
    return false;

  const auto *o = reinterpret_cast<const uint8_t *>(&addr.s_addr);
  if (o[0] == 0 || o[0] == 127)
    return false; // this-network, loopback
  if (o[0] == 10 || (o[0] == 172 && o[1] >= 16 && o[1] <= 31) ||
      (o[0] == 192 && o[1] == 168))
    return false; // RFC1918
  if (o[0] == 169 && o[1] == 254)
    return false; // link-local
  if (o[0] >= 224)
    return false; // multicast, reserved
  return true;
}

bool AddressBook::add(const PeerAddress &peer_address) {
  if (!is_routable(peer_address))
    return false;
  std::lock_guard<std::mutex> book_lock(book_mutex_);
  if (book_.size() >= MAX_ENTRIES)
    return false;
  if (!find(peer_address)) {
    book_.push_back(AddressEntry{.address = peer_address});
    return true;
  }

  return false;
}
std::optional<PeerAddress> AddressBook::select_candidate() {
  auto now = std::chrono::steady_clock::now();
  std::lock_guard<std::mutex> book_lock(book_mutex_);
  for (auto &entry : book_) {
    if (!entry.in_progress && entry.failures < MAX_FAILURES &&
        entry.next_try <= now) {
      entry.in_progress = true;
      return entry.address;
    }
  }

  return std::nullopt;
}
void AddressBook::mark_success(const PeerAddress &peer_address) {
  std::lock_guard<std::mutex> book_lock(book_mutex_);
  auto entry = find(peer_address);
  if (!entry)
    return;
  entry->in_progress = false;
  entry->succeeded = true;
  entry->failures = 0;
}
void AddressBook::mark_failure(const PeerAddress &peer_address) {
  std::lock_guard<std::mutex> book_lock(book_mutex_);
  auto entry = find(peer_address);
  if (!entry)
    return;
  entry->in_progress = false;
  entry->failures++;
  int shift = std::min(entry->failures - 1, 7);
  auto backoff = std::min(BASE_BACKOFF * (1 << shift), MAX_BACKOFF);
  entry->next_try = std::chrono::steady_clock::now() + backoff;
}
std::vector<PeerAddress> AddressBook::reachable() const {
  std::lock_guard<std::mutex> book_lock(book_mutex_);
  std::vector<PeerAddress> active;
  for (const auto &entry : book_) {
    if (entry.succeeded)
      active.push_back(entry.address);
  }

  return active;
}
size_t AddressBook::size() const {
  std::lock_guard<std::mutex> book_lock(book_mutex_);
  return book_.size();
}

AddressBook::AddressEntry *AddressBook::find(const PeerAddress &peer_address) {
  auto it = std::find_if(book_.begin(), book_.end(),
                         [&peer_address](const AddressEntry &entry) {
                           return peer_address.host == entry.address.host &&
                                  peer_address.port == entry.address.port;
                         });

  if (it != book_.end())
    return &*it;
  return nullptr;
}
} // namespace forgechain::network
