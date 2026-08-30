#pragma once

#include "crypto/CommonTypes.hpp"
#include "network/Node.hpp"
#include "network/TcpSocket.hpp"
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
namespace forgechain::app {
class RpcServer {
public:
  RpcServer(network::Node &node, uint16_t port);
  ~RpcServer();

  RpcServer(const RpcServer &) = delete;
  RpcServer &operator=(const RpcServer &) = delete;

  [[nodiscard]] bool start();

  void stop();
  void set_api_key(crypto::str &&api_key);

private:
  void accept_loop();
  void handle_connection(network::TcpSocket socket);
  [[nodiscard]] crypto::str handle_command(const crypto::str &line);
  [[nodiscard]] bool check_api_key(const crypto::str &provided_key) const;
  [[nodiscard]] bool api_key_required() const;
  network::Node &node_;
  uint16_t port_;
  network::TcpSocket listener_{-1};
  std::atomic<bool> running_{false};
  std::thread accept_thread_;
  mutable std::mutex api_key_mutex_;
  crypto::str api_key_;
};
} // namespace forgechain::app
