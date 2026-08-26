#include "network/Handshake.hpp"
#include "crypto/CommonTypes.hpp"
#include "network/Message.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>
namespace forgechain::network {

crypto::bytes serialize_version(const VersionInfo &info) {
  crypto::bytes out;
  out.reserve(kExpectedVersionPayloadSize);
  out.insert(out.end(),
             reinterpret_cast<const uint8_t *>(&info.protocol_version),
             reinterpret_cast<const uint8_t *>(&info.protocol_version) +
                 sizeof(info.protocol_version));

  out.insert(out.end(), reinterpret_cast<const uint8_t *>(&info.chain_height),
             reinterpret_cast<const uint8_t *>(&info.chain_height) +
                 sizeof(info.chain_height));

  out.insert(out.end(), reinterpret_cast<const uint8_t *>(&info.timestamp),
             reinterpret_cast<const uint8_t *>(&info.timestamp) +
                 sizeof(info.timestamp));

  out.insert(out.end(), reinterpret_cast<const uint8_t *>(&info.listen_port),
             reinterpret_cast<const uint8_t *>(&info.listen_port) +
                 sizeof(info.listen_port));
  return out;
}
VersionInfo deserialize_version(const crypto::bytes &payload) {
  VersionInfo info;
  info.protocol_version = *reinterpret_cast<const uint32_t *>(payload.data());
  info.chain_height = *reinterpret_cast<const uint64_t *>(
      payload.data() + sizeof(info.protocol_version));
  info.timestamp = *reinterpret_cast<const uint64_t *>(
      payload.data() + sizeof(info.protocol_version) +
      sizeof(info.chain_height));

  info.listen_port = *reinterpret_cast<const uint16_t *>(
      payload.data() + sizeof(info.protocol_version) +
      sizeof(info.chain_height) + sizeof(info.timestamp));
  return info;
}
std::optional<VersionInfo> perform_handshake(int fd, const VersionInfo &info) {
  Message msg{.type = MessageType::VERSION, .payload = serialize_version(info)};
  if (!send_message(fd, msg))
    return std::nullopt;
  Message incoming_msg;
  if (!receive_message(fd, incoming_msg))
    return std::nullopt;
  if (incoming_msg.type != MessageType::VERSION)
    return std::nullopt;

  if (incoming_msg.payload.size() != kExpectedVersionPayloadSize)
    return std::nullopt;
  return deserialize_version(incoming_msg.payload);
}
} // namespace forgechain::network
