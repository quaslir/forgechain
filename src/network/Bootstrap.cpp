#include "network/Bootstrap.hpp"
#include "crypto/CommonTypes.hpp"
#include "network/PeerAddress.hpp"
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <utility>
#include <vector>
namespace forgechain::network {
std::optional<std::vector<PeerAddress>>
load_bootstrap_peers(const crypto::str &path) {
  std::ifstream file(path);
  if (!file.is_open())
    return std::nullopt;

  crypto::str buffer{};
  std::vector<PeerAddress> peer_addresses{};
  while (file >> buffer) {
    size_t delimeter_pos = buffer.find(':');
    if (delimeter_pos == crypto::str::npos)
      continue;
    crypto::str host = buffer.substr(0, delimeter_pos);
    crypto::str port_str = buffer.substr(delimeter_pos + 1);
    if (host.empty() || port_str.empty())
      continue;
    bool all_digits =
        std::all_of(port_str.begin(), port_str.end(),
                    [](unsigned char c) { return std::isdigit(c); });
    if (!all_digits)
      continue;
    uint16_t port{};
    try {
      int port_num = std::stoi(port_str);
      if (port_num < 0 || port_num > 65535)
        continue;
      port = static_cast<uint16_t>(port_num);
      peer_addresses.push_back(
          PeerAddress{.host = std::move(host), .port = port});
    } catch (...) {
      continue;
    }
  }

  return peer_addresses;
}
} // namespace forgechain::network
