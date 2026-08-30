#pragma once
#include "crypto/CommonTypes.hpp"
#include "crypto/Keys.hpp"
#include "network/PeerAddress.hpp"
#include "network/TcpSocket.hpp"
#include <cstdint>
#include <fstream>
#include <optional>
namespace forgechain::app {
struct RpcConfiguration {
  network::PeerAddress address;
  crypto::str rpc_api_key;
};

struct Config {
  RpcConfiguration rpc_config;
  crypto::str keyfile_path;
};
class Wallet {
public:
  explicit Wallet(const crypto::str &path);
  [[nodiscard]] const crypto::str &address() const;
  [[nodiscard]] std::optional<bool>
  send(const crypto::str &recipient, uint64_t amount, uint64_t fee,
       const RpcConfiguration &rpc_config) const;
  [[nodiscard]] std::optional<crypto::str>
  balance(const RpcConfiguration &rpc_config) const;
  [[nodiscard]] std::optional<crypto::str>
  height(const RpcConfiguration &rpc_config) const;
  [[nodiscard]] std::optional<crypto::str>
  peers(const RpcConfiguration &rpc_config) const;

private:
  static crypto::str read_from(network::TcpSocket socket);
  [[nodiscard]] bool load_from_file(std::ifstream &file);
  void save_to_file(const crypto::str &path);
  crypto::KeyPair keys_;
  crypto::str address_;
};
} // namespace forgechain::app
