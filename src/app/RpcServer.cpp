#include "app/RpcServer.hpp"
#include "core/Transaction.hpp"

#include "crypto/CommonTypes.hpp"
#include "crypto/Hash.hpp"
#include "network/Node.hpp"
#include "network/Socket.hpp"
#include "network/TcpSocket.hpp"
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
namespace forgechain::app {
RpcServer::RpcServer(network::Node &node, uint16_t port)
    : node_(node), port_(port) {}

bool RpcServer::start() {
  listener_ = network::listen_on(port_);
  if (!listener_.is_valid()) {
    return false;
  }
  listener_.set_receive_timeout(1);
  running_.store(true);
  accept_thread_ = std::thread(&RpcServer::accept_loop, this);
  return true;
}

void RpcServer::accept_loop() {
  while (running_) {
    network::TcpSocket socket = network::accept_connection(listener_);
    if (!socket.is_valid())
      continue;
    handle_connection(std::move(socket));
  }
}
void RpcServer::handle_connection(network::TcpSocket socket) {
  crypto::str line{};
  uint8_t byte{};
  while (network::read_exact(socket.fd(), &byte, 1)) {
    if (line.size() >= 4096) {
      crypto::str error_response{"ERROR"};
      network::send_exact(
          socket.fd(), reinterpret_cast<const uint8_t *>(error_response.data()),
          error_response.size());
      return;
    }
    if (byte == '\n') {
      break;
    }
    line.push_back(static_cast<char>(byte));
  }

  crypto::str result = handle_command(line) + '\n';
  network::send_exact(socket.fd(),
                      reinterpret_cast<const uint8_t *>(result.data()),
                      result.size());
}

crypto::str RpcServer::handle_command(const crypto::str &line) {
  std::istringstream iss(line);
  crypto::str command{};
  iss >> command;
  if (command == "GETBALANCE") {
    crypto::str address{};
    iss >> address;
    if (address.empty())
      return "ERROR_EMPTY_ADDRESS";
    auto balance = node_.get_balance(address);
    if (balance.has_value()) {
      return std::to_string(*balance);
    }
    return "UNKNOWN";
  } else if (command == "SUBMITTX") {
    crypto::str hex;
    iss >> hex;
    if (hex.empty())
      return "ERROR_EMPTY_PAYLOAD";
    auto bytes = crypto::from_hex(hex);
    if (!bytes.has_value()) {
      return "ERROR_INVALID_HEX";
    }

    auto tx = core::Transaction::deserialize(*bytes);
    if (!tx.has_value()) {
      return "ERROR_INVALID_PAYLOAD";
    }

    node_.submit_transaction(*tx);
    return "OK";
  } else if (command == "HEIGHT") {
    return std::to_string(node_.chain_height());
  } else if (command == "PEERS") {
    return std::to_string(node_.peer_count());
  }

  return "ERROR_UNKNOWN_COMMAND";
}
void RpcServer::stop() {
  running_.store(false);
  listener_.close_socket();
  if (accept_thread_.joinable()) {
    accept_thread_.join();
  }
}
RpcServer::~RpcServer() { stop(); }
} // namespace forgechain::app
