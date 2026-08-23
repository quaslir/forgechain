#include "app/Wallet.hpp"
#include "core/Transaction.hpp"
#include "crypto/Address.hpp"
#include "crypto/CommonTypes.hpp"
#include "crypto/Hash.hpp"
#include "crypto/Keys.hpp"
#include "crypto/Signature.hpp"
#include "network/Handshake.hpp"
#include "network/Message.hpp"
#include "network/TcpSocket.hpp"
#include <cstdint>
#include <fstream>
#include <stdexcept>
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
  auto read_and_check = [](std::ifstream &file, crypto::str &buffer) -> bool {
    file >> buffer;
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

bool Wallet::send(const crypto::str &recipient, uint64_t amount,
                  const crypto::str &host, uint16_t port) {
  core::Transaction tx{address_, recipient, amount, keys_.public_key};
  tx.signature_ = crypto::sign(tx.serialize_for_signing(), keys_.private_key);
  network::TcpSocket socket = network::connect_to(host, port);
  if (!socket.is_valid())
    return false;
  auto info = network::perform_handshake(
      socket.fd(), network::VersionInfo{.protocol_version = 1,
                                        .chain_height = 0,
                                        .timestamp = 0});
  if (!info.has_value())
    return false;
  network::Message msg{.type = network::MessageType::TX,
                       .payload = tx.serialize()};
  return network::send_message(socket.fd(), msg);
}

const crypto::str& Wallet::address() const {return address_;}
} // namespace forgechain::app
