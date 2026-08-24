#include "app/Wallet.hpp"
#include "core/Transaction.hpp"
#include "crypto/Address.hpp"
#include "crypto/CommonTypes.hpp"
#include "crypto/Hash.hpp"
#include "crypto/Keys.hpp"
#include "crypto/Signature.hpp"
#include "network/Socket.hpp"
#include "network/TcpSocket.hpp"
#include <cstdint>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <utility>
namespace forgechain::app {
Wallet::Wallet(const crypto::str &path) {
  std::ifstream file(path);
  if (file.is_open()) {
    if (!load_from_file(file)) {
      throw std::runtime_error("malformed keyfile: " + path);
    }
  } else {
    keys_ = crypto::generate_keypair();
    save_to_file(path);
  }
  address_ = crypto::derive_address(keys_.public_key);
}
bool Wallet::load_from_file(std::ifstream &file) {
  auto read_and_check = [](std::ifstream &file_, crypto::str &buffer) -> bool {
    buffer.clear();
    file_ >> buffer;
    return !buffer.empty();
  };
  crypto::str buffer{};
  if (!read_and_check(file, buffer))
    return false;
  auto private_key = crypto::from_hex(buffer);
  if (!private_key.has_value())
    return false;
  keys_.private_key = *private_key;
  if (!read_and_check(file, buffer))
    return false;
  auto public_key = crypto::from_hex(buffer);
  if (!public_key.has_value())
    return false;
  keys_.public_key = *public_key;

  return true;
}
void Wallet::save_to_file(const crypto::str &path) {
  std::ofstream file(path);
  if (!file.is_open()) {
    throw std::runtime_error("failed to create keyfile: " + path);
  }
  file << crypto::to_hex(keys_.private_key) << std::endl;
  file << crypto::to_hex(keys_.public_key) << std::endl;
}

crypto::str Wallet::read_from(network::TcpSocket socket) {
  crypto::str line{};
  uint8_t byte{};

  while (network::read_exact(socket.fd(), &byte, 1)) {
    if (byte == '\n')
      break;
    line.push_back(static_cast<char>(byte));
  }

  return line;
}

std::optional<bool> Wallet::send(const crypto::str &recipient, uint64_t amount,
                                 const crypto::str &host, uint16_t port) const {
  core::Transaction tx{address_, recipient, amount, keys_.public_key};
  tx.signature_ = crypto::sign(tx.serialize_for_signing(), keys_.private_key);
  network::TcpSocket socket = network::connect_to(host, port);
  if (!socket.is_valid())
    return std::nullopt;

  crypto::str hex = crypto::to_hex(tx.serialize());
  crypto::str command = "SUBMITTX " + hex + '\n';
  if (!network::send_exact(socket.fd(),
                           reinterpret_cast<const uint8_t *>(command.data()),
                           command.size())) {
    return std::nullopt;
  }

  return read_from(std::move(socket)) == "OK";
}

std::optional<crypto::str> Wallet::balance(const crypto::str &host,
                                           uint16_t port) const {
  network::TcpSocket socket = network::connect_to(host, port);
  if (!socket.is_valid()) {
    return std::nullopt;
  }
  crypto::str command = "GETBALANCE " + address_ + '\n';
  if (!network::send_exact(socket.fd(),
                           reinterpret_cast<const uint8_t *>(command.data()),
                           command.size())) {
    return std::nullopt;
  }

  return read_from(std::move(socket));
}

std::optional<crypto::str> Wallet::height(const crypto::str &host,
                                          uint16_t port) const {
  network::TcpSocket socket = network::connect_to(host, port);
  if (!socket.is_valid()) {
    return std::nullopt;
  }

  crypto::str command{"HEIGHT\n"};
  if (!network::send_exact(socket.fd(),
                           reinterpret_cast<const uint8_t *>(command.data()),
                           command.size())) {
    return std::nullopt;
  }

  return read_from(std::move(socket));
}
[[nodiscard]] std::optional<crypto::str> Wallet::peers(const crypto::str &host,
                                                       uint16_t port) const {
  network::TcpSocket socket = network::connect_to(host, port);
  if (!socket.is_valid()) {
    return std::nullopt;
  }

  crypto::str command{"PEERS\n"};
  if (!network::send_exact(socket.fd(),
                           reinterpret_cast<const uint8_t *>(command.data()),
                           command.size())) {
    return std::nullopt;
  }

  return read_from(std::move(socket));
}

const crypto::str &Wallet::address() const { return address_; }
} // namespace forgechain::app
