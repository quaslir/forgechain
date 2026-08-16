#include "network/Message.hpp"
#include "crypto/CommonTypes.hpp"
#include "network/Socket.hpp"
#include <cstddef>
#include <cstdint>
namespace forgechain::network {
bool send_message(int fd, const Message &message) {
  crypto::bytes header{MAGIC_BYTES.begin(), MAGIC_BYTES.end()};
  auto cmd = static_cast<uint8_t>(message.type);
  header.push_back(cmd);
  auto payload_length = static_cast<uint32_t>(message.payload.size());
  if (payload_length > MAX_PAYLOAD_SIZE)
    return false;
  const auto *length_bytes = reinterpret_cast<const uint8_t *>(&payload_length);
  header.insert(header.end(), length_bytes,
                length_bytes + sizeof(payload_length));
  if (!send_exact(fd, header.data(), HEADER_LENGTH))
    return false;
  return send_exact(fd, message.payload.data(), message.payload.size());
}
bool receive_message(int fd, Message &message) {
  crypto::bytes header_buffer(HEADER_LENGTH);
  if (!read_exact(fd, header_buffer.data(), HEADER_LENGTH))
    return false;

  // check magic
  for (size_t i = 0; i < 4; i++) {
    if (header_buffer[i] != MAGIC_BYTES[i])
      return false;
  }
  // fill type
  message.type = static_cast<MessageType>(header_buffer[4]);
  // fill size
  uint32_t length =
      *reinterpret_cast<const uint32_t *>(header_buffer.data() + 5);
  if (length > MAX_PAYLOAD_SIZE)
    return false;
  message.payload.resize(length);
  return read_exact(fd, message.payload.data(), length);
}
} // namespace forgechain::network
