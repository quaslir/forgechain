#pragma once
#include "crypto/CommonTypes.hpp"
#include "crypto/Keys.hpp"
#include <cstdint>
#include <fstream>
namespace forgechain::app {
class Wallet {
public:
  explicit Wallet(const crypto::str &path);
  [[nodiscard]] const crypto::str &address() const;
  [[nodiscard]] bool send(const crypto::str &recipient, uint64_t amount,
                          const crypto::str &host, uint16_t port);

private:
  [[nodiscard]] bool load_from_file(std::ifstream &file);
  void save_to_file(const crypto::str &path);
  crypto::KeyPair keys_;
  crypto::str address_;
};
} // namespace forgechain::app
