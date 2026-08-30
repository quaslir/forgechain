#include "app/WalletCli.hpp"
#include "app/ParseNumber.hpp"
#include "app/Wallet.hpp"
#include "crypto/CommonTypes.hpp"
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
namespace forgechain::app {

WalletCli::WalletCli(Wallet wallet, RpcConfiguration rpc_config)
    : wallet_(std::move(wallet)), rpc_config_(std::move(rpc_config)) {}
void WalletCli::run() {
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
      handle_help_command();
    } else if (command == "connect") {
      handle_connect_command(iss);
    } else if (command == "set") {
      handle_set_command(iss);
    } else if (command == "exit" || command == "quit") {
      break;
    } else if (command == "balance") {
      handle_balance_command();
    } else if (command == "send") {
      handle_send_command(iss);

    } else if (command == "peers") {
      handle_peers_command();
    } else if (command == "height") {
      handle_height_command();
    }

    else {
      std::cerr << "unknown command: " << command << std::endl;
    }
  }
}

void WalletCli::handle_help_command() {
  std::cout << "commands:" << std::endl;
  std::cout << "  send <address> <amount> [fee]  sign and submit a transaction"
            << std::endl;
  std::cout << "  balance                  show this wallet's balance"
            << std::endl;
  std::cout << "  height                   show connected node's chain height"
            << std::endl;
  std::cout << "  peers                    show connected node's peer count"
            << std::endl;
  std::cout << "  connect <host> <port>    change which node's RPC port to "
               "talk to"
            << std::endl;
  std::cout << "  set rpc-api-key <value>  set the RPC auth token to send with "
               "requests"
            << std::endl;
  std::cout << "  help                     show this message" << std::endl;
  std::cout << "  quit / exit              exit the wallet" << std::endl;
}
void WalletCli::handle_connect_command(std::istringstream &iss) {
  crypto::str host{}, port{};
  if (!(iss >> host >> port)) {
    std::cerr << "usage: connect <host> <port>" << std::endl;
    return;
  }

  auto port_number = app::parse_number(port);
  if (!port_number.has_value()) {
    std::cerr << "invalid port: " << port << std::endl;
    return;
  }

  rpc_config_.address.host = std::move(host);
  rpc_config_.address.port = static_cast<uint16_t>(*port_number);
  std::cout << "target set to " << rpc_config_.address.host << ":"
            << rpc_config_.address.port << std::endl;
}
void WalletCli::handle_set_command(std::istringstream &iss) {
  crypto::str subcommand{};
  iss >> subcommand;
  if (subcommand.empty()) {
    std::cerr << "usage: set rpc-api-key <value>" << std::endl;
    return;
  }
  if (subcommand == "rpc-api-key") {
    crypto::str api_key{};
    iss >> api_key;
    if (api_key.empty()) {
      std::cerr << "usage: set rpc-api-key <value>" << std::endl;
      return;
    }
    rpc_config_.rpc_api_key = std::move(api_key);
    std::cout << "RPC API key set" << std::endl;
  } else {
    std::cerr << "unknown set target: " << subcommand << std::endl;
    return;
  }
}
void WalletCli::handle_balance_command() {
  auto result = wallet_.balance(rpc_config_);
  if (result.has_value()) {
    std::cout << *result << std::endl;
  } else
    std::cerr << "network error" << std::endl;
}
void WalletCli::handle_send_command(std::istringstream &iss) {
  crypto::str recipient{}, amount_str{}, fee_str;
  iss >> recipient >> amount_str >> fee_str;

  if (recipient.empty() || amount_str.empty()) {
    std::cerr << "usage: send <address> <amount> (optional) <fee>" << std::endl;
    return;
  }
  auto amount = app::parse_number(amount_str);
  if (!amount.has_value()) {
    std::cerr << "invalid amount: " << amount_str << std::endl;
    return;
  }

  int fee{0};
  if (!fee_str.empty()) {
    auto fee_num = app::parse_number(fee_str);
    if (fee_num.has_value()) {
      fee = *fee_num;
    } else {
      std::cerr << "invalid fee: " << fee_str << std::endl;
      return;
    }
  }

  auto ok = wallet_.send(recipient, static_cast<uint64_t>(*amount),
                         static_cast<uint64_t>(fee), rpc_config_);
  if (!ok.has_value()) {
    std::cerr << "network error" << std::endl;
  } else if (*ok) {
    std::cout << "sent" << std::endl;
  } else {
    std::cerr << "rejected by node" << std::endl;
  }
}
void WalletCli::handle_peers_command() {
  auto peers_number = wallet_.peers(rpc_config_);
  if (peers_number.has_value()) {
    std::cout << *peers_number << std::endl;
  } else
    std::cerr << "network error" << std::endl;
}
void WalletCli::handle_height_command() {
  auto height = wallet_.height(rpc_config_);
  if (height.has_value()) {
    std::cout << *height << std::endl;
  } else
    std::cerr << "network error" << std::endl;
}
} // namespace forgechain::app
