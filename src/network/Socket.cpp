#include "network/Socket.hpp"
#include <cstddef>
#include <cstdint>
#include <sys/socket.h>
#include <sys/types.h>
namespace forgechain::network {
bool read_exact(int fd, uint8_t *buffer, size_t length) {
  size_t total_read = 0;
  while (total_read < length) {
    ssize_t n = recv(fd, buffer + total_read, length - total_read, 0);
    if (n <= 0) {
      return false;
    }

    total_read += static_cast<size_t>(n);
  }

  return true;
}
bool send_exact(int fd, const uint8_t *buffer, size_t length) {
  size_t total_sent = 0;
  while (total_sent < length) {
    ssize_t n = send(fd, buffer + total_sent, length - total_sent, 0);
    if (n <= 0) {
      return false;
    }
    total_sent += static_cast<size_t>(n);
  }

  return true;
}
} // namespace forgechain::network
