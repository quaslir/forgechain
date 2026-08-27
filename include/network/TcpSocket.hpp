#pragma once

#include "crypto/CommonTypes.hpp"
#include <atomic>
#include <cstdint>
#include <optional>
namespace forgechain::network {
class TcpSocket {
public:
  explicit TcpSocket(int fd);
  ~TcpSocket();

  TcpSocket(const TcpSocket &) = delete;
  TcpSocket &operator=(const TcpSocket &) = delete;
  TcpSocket(TcpSocket &&other) noexcept;
  TcpSocket &operator=(TcpSocket &&other) noexcept;

  [[nodiscard]] int fd() const;
  [[nodiscard]] bool is_valid() const;
  void close_socket();
  void set_receive_timeout(int seconds);

private:
  std::atomic<int> fd_;
};

TcpSocket listen_on(uint16_t port);
TcpSocket accept_connection(const TcpSocket &listener);
TcpSocket connect_to(const crypto::str &host, uint16_t port);
std::optional<crypto::str> get_peer_ip(int fd);
} // namespace forgechain::network
