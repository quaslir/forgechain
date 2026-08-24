#include "app/Wallet.hpp"
#include "crypto/CommonTypes.hpp"
#include <cstddef>
#include <exception>
#include <span>
#include <sstream>
#include <string_view>
#include <stdexcept>
#include "network/PeerAddress.hpp"
#include <cstdint>
#include <optional>
#include <charconv>
#include <string>
#include <utility>
#include <iostream>
using namespace forgechain;


struct Config {
    network::PeerAddress address;
    crypto::str keyfile_path;
};

auto parse_number = [](std::string_view str_number) -> std::optional<int> {
  int number{0};

  auto result = std::from_chars(
      str_number.data(), str_number.data() + str_number.size(), number);
  if (result.ec != std::errc{}) {
    return std::nullopt;
  }

  return number;
};

Config parse_args(std::span<char *> argv) {

    Config config;
    for(size_t i = 1; i < argv.size(); i++) {
        std::string_view  view{argv[i]};
        if (((view == "--connect") || (view == "-c")) &&
                   i + 2 < argv.size()) {
          network::PeerAddress address;
          address.host = std::string{argv[i + 1]};
          std::string_view port_view{argv[i + 2]};
          auto connect_port = parse_number(port_view);
          if (!connect_port.has_value()) {
            throw std::invalid_argument("invalid connect port");
          }

          address.port = static_cast<uint16_t>(*connect_port);
          config.address = std::move(address);
          i += 2;
        } else if(((view == "--keyfile") || (view == "-k")) &&
                   i + 1 < argv.size()) {
                       ++i;
                       std::string_view view_path{argv[i]};
                       if(view_path.empty()) {
                           throw std::invalid_argument("invalid path to keys");
                       }
                       config.keyfile_path= argv[i];
                   }
    }
    if (config.keyfile_path.empty()) {
      throw std::invalid_argument("missing required --keyfile <path>");
    }
    if (config.address.host.empty() || config.address.port == 0) {
      throw std::invalid_argument("missing required --connect <host> <port>");
    }
return config;
}

int main(int argc, char * argv[]) {
Config config;
try {
    config = parse_args(std::span<char *>(argv, static_cast<size_t>(argc)));
} catch(const std::exception& err) {
    std::cerr << "Error: " << err.what() << std::endl;
    return 1;
}
std::optional<app::Wallet> wallet;
try {
    wallet.emplace(config.keyfile_path);

} catch(const std::exception& err) {
    std::cerr << "Error: " << err.what() << std::endl;
    return 2;
}
std::cout << "Wallet address: " << wallet->address() << std::endl;


    for(;;) {
    crypto::str buffer{};
    std::cout << ">>> ";
    if(!std::getline(std::cin, buffer)) {
        break;
    }
    std::istringstream iss(buffer);
    crypto::str command{};
    iss >> command;
    if(command.empty()) continue;

    if(command == "exit" || command == "quit") {
        break;
    }

    if(command == "send") {
        crypto::str recipient{}, amount_str{};
        iss >> recipient >> amount_str;

        if(recipient.empty() || amount_str.empty()) {
            std::cerr << "usage: send <address> <amount>" << std::endl;
            continue;
        }
        auto amount = parse_number(amount_str);
        if(!amount.has_value()) {
            std::cerr << "invalid amount: " << amount_str << std::endl;
            continue;
        }

        bool ok = wallet->send(recipient, static_cast<uint64_t>(*amount), config.address.host, config.address.port);

        if(ok) {
            std::cout << "sent" << std::endl;
        } else {
            std::cerr << "failed to send (node unreachable?)"  << std::endl;
        }

    } else {
        std::cerr << "unknown command: " << command << std::endl;
    }
}
}
