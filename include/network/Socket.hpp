#pragma once

#include <cstddef>
#include <cstdint>
 namespace forgechain::network {
     bool read_exact(int fd, uint8_t * buffer, size_t length);
     bool send_exact(int fd, const uint8_t * buffer, size_t length);
 }
