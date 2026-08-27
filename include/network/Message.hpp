#pragma once

#include "crypto/CommonTypes.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
namespace forgechain::network {
constexpr std::array<uint8_t, 4> MAGIC_BYTES = {0x46, 0x52, 0x47, 0x43};
constexpr size_t HEADER_LENGTH = 9;
constexpr uint32_t MAX_PAYLOAD_SIZE = 16 * 1024 * 1024;
enum class MessageType : uint8_t {
  VERSION = 0x00,
  INV = 0x01,
  GETDATA = 0x02,
  BLOCK = 0x03,
  TX = 0x04,
  GETBLOCKS = 0x05,
  PING = 0x06,
  PONG = 0x07,
  PEERS = 0x08
};

struct Message {
  MessageType type;
  crypto::bytes payload;
};

bool send_message(int fd, const Message &message);
bool receive_message(int fd, Message &message);
} // namespace forgechain::network
