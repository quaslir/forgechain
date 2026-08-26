#pragma once

#include "crypto/CommonTypes.hpp"
#include "network/Handshake.hpp"
#include "network/Heartbeat.hpp"
#include "network/TcpSocket.hpp"
#include <atomic>
#include <chrono>
namespace forgechain::network {
class Peer {
public:
  Peer(TcpSocket socket, VersionInfo remote_version, crypto::str host);
  Peer(const Peer &) = delete;
  Peer &operator=(const Peer &) = delete;
  Peer(Peer &&) noexcept;
  Peer &operator=(Peer &&) noexcept;

  TcpSocket &socket();
  [[nodiscard]] const VersionInfo &remote_version() const;
  [[nodiscard]] const crypto::str &host() const;
  [[nodiscard]] bool is_alive() const;
  void mark_dead();

  void touch();
  [[nodiscard]] std::chrono::seconds elapsed() const;

private:
  TcpSocket socket_;
  VersionInfo remote_version_;
  std::atomic<bool> alive_{true};
  Heartbeat heartbeat_;
  crypto::str host_;
};
} // namespace forgechain::network
