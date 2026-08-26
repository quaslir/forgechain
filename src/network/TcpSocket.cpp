#include "network/TcpSocket.hpp"
#include "crypto/CommonTypes.hpp"
#include <arpa/inet.h>
#include <array>
#include <cstdint>
#include <netinet/in.h>
#include <optional>
#include <sys/socket.h>
#include <unistd.h>
namespace forgechain::network {
TcpSocket::TcpSocket(int fd) : fd_(fd) {}
void TcpSocket::close_socket() {
  if (is_valid()) {
    close(fd_.load());
    fd_.store(-1);
  }
}
TcpSocket::~TcpSocket() { close_socket(); }

TcpSocket::TcpSocket(TcpSocket &&other) noexcept {
  fd_.store(other.fd());
  other.fd_.store(-1);
}
TcpSocket &TcpSocket::operator=(TcpSocket &&other) noexcept {
  if (this != &other) {
    if (is_valid()) {
      close(fd_.load());
    }
    fd_.store(other.fd());
    other.fd_.store(-1);
  }
  return *this;
}

int TcpSocket::fd() const { return fd_.load(); }
bool TcpSocket::is_valid() const { return fd_.load() >= 0; }
void TcpSocket::set_receive_timeout(int seconds) {
  timeval tv{};
  tv.tv_sec = seconds;
  tv.tv_usec = 0;
  setsockopt(fd_.load(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}
TcpSocket listen_on(uint16_t port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return TcpSocket{-1};
  }
  int opt = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = INADDR_ANY;
  address.sin_port = htons(port);
  int res = bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address));
  if (res < 0) {
    return TcpSocket{-1};
  }
  if (listen(fd, 5) < 0) {
    return TcpSocket{-1};
  }

  return TcpSocket{fd};
}
TcpSocket accept_connection(const TcpSocket &listener) {
  int fd = accept(listener.fd(), nullptr, nullptr);
  if (fd < 0) {
    return TcpSocket{-1};
  }

  return TcpSocket{fd};
}

TcpSocket connect_to(const crypto::str &host, uint16_t port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return TcpSocket{-1};
  }
  sockaddr_in address{};
  int res = inet_pton(AF_INET, host.c_str(), &address.sin_addr);
  if (res != 1) {
    return TcpSocket{-1};
  }
  address.sin_port = htons(port);
  address.sin_family = AF_INET;

  if (connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) <
      0) {
    return TcpSocket{-1};
  }

  return TcpSocket{fd};
}
std::optional<crypto::str> get_peer_ip(int fd) {
  if (fd < 0)
    return std::nullopt;
  sockaddr_in address{};
  socklen_t address_length = sizeof(address);
  int result =
      getpeername(fd, reinterpret_cast<sockaddr *>(&address), &address_length);
  if (result < 0) {
    return std::nullopt;
  }

  std::array<char, INET_ADDRSTRLEN> buffer;
  if (!inet_ntop(AF_INET, &address.sin_addr, buffer.data(), buffer.size()))
    return std::nullopt;

  return crypto::str(buffer.data());
}
} // namespace forgechain::network
