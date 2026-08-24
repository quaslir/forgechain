#pragma once

#include "crypto/CommonTypes.hpp"
#include "network/Node.hpp"
#include "network/TcpSocket.hpp"
#include <atomic>
#include <cstdint>
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

private:
  void accept_loop();
  void handle_connection(network::TcpSocket socket);
  [[nodiscard]] crypto::str handle_command(const crypto::str &line);

  network::Node &node_;
  uint16_t port_;
  network::TcpSocket listener_{-1};
  std::atomic<bool> running_{false};
  std::thread accept_thread_;
};
} // namespace forgechain::app
