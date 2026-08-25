#include "app/Wallet.hpp"
#include "crypto/CommonTypes.hpp"
#include "network/PeerAddress.hpp"
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include "app/ParseNumber.hpp"
using namespace forgechain;

struct Config {
  network::PeerAddress address;
  crypto::str keyfile_path;
};


Config parse_args(std::span<char *> argv) {

  Config config;
  for (size_t i = 1; i < argv.size(); i++) {
    std::string_view view{argv[i]};
    if (((view == "--connect") || (view == "-c")) && i + 2 < argv.size()) {
      network::PeerAddress address;
      address.host = std::string{argv[i + 1]};
      std::string_view port_view{argv[i + 2]};
      auto connect_port = app::parse_number(port_view);
      if (!connect_port.has_value()) {
        throw std::invalid_argument("invalid connect port");
      }

      address.port = static_cast<uint16_t>(*connect_port);
      config.address = std::move(address);
      i += 2;
    } else if (((view == "--keyfile") || (view == "-k")) &&
               i + 1 < argv.size()) {
      ++i;
      std::string_view view_path{argv[i]};
      if (view_path.empty()) {
        throw std::invalid_argument("invalid path to keys");
      }
      config.keyfile_path = argv[i];
    }
  }
  if (config.keyfile_path.empty()) {
    throw std::invalid_argument("missing required --keyfile <path>");
  }
  return config;
}

int main(int argc, char *argv[]) {
  Config config;
  try {
    config = parse_args(std::span<char *>(argv, static_cast<size_t>(argc)));
  } catch (const std::exception &err) {
    std::cerr << "Error: " << err.what() << std::endl;
    return 1;
  }
  std::optional<app::Wallet> wallet;
  try {
    wallet.emplace(config.keyfile_path);

  } catch (const std::exception &err) {
    std::cerr << "Error: " << err.what() << std::endl;
    return 2;
  }
  std::cout << "Wallet address: " << wallet->address() << std::endl;

  for (;;) {
    crypto::str buffer{};
    std::cout << ">>> ";
    if (!std::getline(std::cin, buffer)) {
      break;
    }
    std::istringstream iss(buffer);
    crypto::str command{};
    iss >> command;
    if (command.empty())
      continue;
    else if (command == "help") {
        std::cout << "commands:" << std::endl;
        std::cout << "  send <address> <amount>  sign and submit a transaction" << std::endl;
        std::cout << "  balance                  show this wallet's balance" << std::endl;
        std::cout << "  height                   show connected node's chain height" << std::endl;
        std::cout << "  peers                    show connected node's peer count" << std::endl;
        std::cout << "  connect <host> <port>    change which node's RPC port to talk to" << std::endl;
        std::cout << "  help                     show this message" << std::endl;
        std::cout << "  quit / exit              exit the wallet" << std::endl;
    }
    else if(command == "connect") {
        crypto::str host{}, port{};
        if(!(iss >> host >> port)) {
            std::cerr << "usage: connect <host> <port>" << std::endl;
            continue;
        }

        auto port_number = app::parse_number(port);
        if(!port_number.has_value()) {
            std::cerr << "invalid port: " << port << std::endl;
                    continue;
        }

        config.address.host = std::move(host);
        config.address.port = static_cast<uint16_t>(*port_number);
        std::cout << "target set to " << config.address.host << ":"
                       << config.address.port << std::endl;
    }
    else if (command == "exit" || command == "quit") {
      break;
    } else if (command == "balance") {
      auto result = wallet->balance(config.address.host, config.address.port);
      if (result.has_value()) {
        std::cout << *result << std::endl;
      } else
        std::cerr << "network error" << std::endl;
    } else if (command == "send") {
      crypto::str recipient{}, amount_str{};
      iss >> recipient >> amount_str;

      if (recipient.empty() || amount_str.empty()) {
        std::cerr << "usage: send <address> <amount>" << std::endl;
        continue;
      }
      auto amount = app::parse_number(amount_str);
      if (!amount.has_value()) {
        std::cerr << "invalid amount: " << amount_str << std::endl;
        continue;
      }

      auto ok = wallet->send(recipient, static_cast<uint64_t>(*amount),
                             config.address.host, config.address.port);
      if (!ok.has_value()) {
        std::cerr << "network error" << std::endl;
      } else if (*ok) {
        std::cout << "sent" << std::endl;
      } else {
        std::cerr << "rejected by node" << std::endl;
      }

    } else if (command == "peers") {
      auto peers_number =
          wallet->peers(config.address.host, config.address.port);
      if (peers_number.has_value()) {
        std::cout << *peers_number << std::endl;
      } else
        std::cerr << "network error" << std::endl;
    } else if (command == "height") {
      auto height = wallet->height(config.address.host, config.address.port);
      if (height.has_value()) {
        std::cout << *height << std::endl;
      } else
        std::cerr << "network error" << std::endl;
    }

    else {
      std::cerr << "unknown command: " << command << std::endl;
    }
  }
}
