#pragma once

#include "crypto/CommonTypes.hpp"
#include "network/Handshake.hpp"
#include "network/Heartbeat.hpp"
#include "network/TcpSocket.hpp"
#include <atomic>
#include <chrono>
#include <mutex>
namespace forgechain::network {
class Peer {
public:
  Peer(TcpSocket socket, VersionInfo remote_version, crypto::str host);
  Peer(const Peer &) = delete;
  Peer &operator=(const Peer &) = delete;
  Peer(Peer &&) = delete;
  Peer &operator=(Peer &&) = delete;

  TcpSocket &socket();
  [[nodiscard]] const VersionInfo &remote_version() const;
  [[nodiscard]] const crypto::str &host() const;
  [[nodiscard]] bool is_alive() const;
  void mark_dead();

  void touch();
  [[nodiscard]] std::chrono::seconds elapsed() const;
  [[nodiscard]] std::mutex &write_mutex() const;

private:
  TcpSocket socket_;
  VersionInfo remote_version_;
  std::atomic<bool> alive_{true};
  Heartbeat heartbeat_;
  crypto::str host_;
  mutable std::mutex write_mutex_;
};
} // namespace forgechain::network
