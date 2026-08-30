#pragma once
#include "app/Wallet.hpp"
#include <sstream>
namespace forgechain::app {
class WalletCli {
public:
  WalletCli(Wallet wallet, RpcConfiguration rpc_config);
  void run();

private:
  void handle_help_command();
  void handle_connect_command(std::istringstream &iss);
  void handle_set_command(std::istringstream &iss);
  void handle_balance_command();
  void handle_send_command(std::istringstream &iss);
  void handle_peers_command();
  void handle_height_command();

  Wallet wallet_;
  RpcConfiguration rpc_config_;
};
} // namespace forgechain::app
