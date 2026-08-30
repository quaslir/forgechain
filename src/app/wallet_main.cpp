#include "app/ParseNumber.hpp"
#include "app/Wallet.hpp"
#include "app/WalletCli.hpp"
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
using namespace forgechain;
using app::Config;
using app::RpcConfiguration;

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
      config.rpc_config.address = std::move(address);
      i += 2;
    } else if (((view == "--keyfile") || (view == "-k")) &&
               i + 1 < argv.size()) {
      ++i;
      std::string_view view_path{argv[i]};
      if (view_path.empty()) {
        throw std::invalid_argument("invalid path to keys");
      }
      config.keyfile_path = argv[i];
    } else if (((view == "--rpc-api-key") || (view == "-K")) &&
               i + 1 < argv.size()) {
      i++;
      std::string_view api_key_view{argv[i]};
      if (api_key_view.empty()) {
        throw std::invalid_argument("empty api key");
      }
      config.rpc_config.rpc_api_key = api_key_view;
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

  app::WalletCli wallet_cli{std::move(*wallet), config.rpc_config};
  wallet_cli.run();
  return 0;
}
