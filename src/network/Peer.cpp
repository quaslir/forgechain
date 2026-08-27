#include "network/Peer.hpp"
#include "crypto/CommonTypes.hpp"
#include "network/Handshake.hpp"
#include "network/Heartbeat.hpp"
#include "network/TcpSocket.hpp"
#include <chrono>
#include <utility>
namespace forgechain::network {
Peer::Peer(TcpSocket socket, VersionInfo remote_version, crypto::str host)
    : socket_(std::move(socket)), remote_version_(remote_version),
      host_(std::move(host)) {}

Peer::Peer(Peer &&other) noexcept
    : socket_(std::move(other.socket_)), remote_version_(other.remote_version_),
      alive_(other.alive_.load()), heartbeat_(std::move(other.heartbeat_)),
      host_(std::move(other.host_)) {}
Peer &Peer::operator=(Peer &&other) noexcept {
  if (this != &other) {
    socket_ = std::move(other.socket_);
    remote_version_ = other.remote_version_;
    alive_.store(other.alive_.load());
    heartbeat_ = std::move(other.heartbeat_);
    host_ = std::move(other.host_);
  }
  return *this;
}
const VersionInfo &Peer::remote_version() const { return remote_version_; }
TcpSocket &Peer::socket() { return socket_; }
const crypto::str &Peer::host() const { return host_; }
[[nodiscard]] bool Peer::is_alive() const { return alive_.load(); }
void Peer::mark_dead() { alive_.store(false); }

void Peer::touch() { heartbeat_.touch(); }
[[nodiscard]] std::chrono::seconds Peer::elapsed() const {
  return heartbeat_.elapsed();
}
} // namespace forgechain::network
