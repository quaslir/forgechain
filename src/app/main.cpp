#include "app/Orchestrator.hpp"
#include "crypto/CommonTypes.hpp"
#include "network/Bootstrap.hpp"
#include "network/PeerAddress.hpp"
#include <charconv>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace {
forgechain::app::Orchestrator *g_orchestrator{nullptr};

void handle_sigint(int) {
  if (g_orchestrator) {
    g_orchestrator->stop();
  }

  std::_Exit(0);
}
} // namespace

void parse_args(std::span<char *> argv,
                forgechain::app::OrchestratorConfig &config,
                forgechain::crypto::str &file_bootstrap_path) {
  auto parse_number = [](std::string_view str_number) -> std::optional<int> {
    int number{0};

    auto result = std::from_chars(
        str_number.data(), str_number.data() + str_number.size(), number);
    if (result.ec != std::errc{}) {
      return std::nullopt;
    }

    return number;
  };
  for (size_t i = 1; i < argv.size(); i++) {
    std::string_view view{argv[i]};

    if (((view == "--port") || (view == "-p")) && i + 1 < argv.size()) {
      ++i;
      std::string_view port_view{argv[i]};
      auto port = parse_number(port_view);
      if (!port.has_value()) {
        throw std::invalid_argument("invalid port");
      }

      config.listen_port = static_cast<uint16_t>(*port);
    } else if (((view == "--connect") || (view == "-c")) &&
               i + 2 < argv.size()) {
      forgechain::network::PeerAddress address;
      address.host = std::string{argv[i + 1]};
      std::string_view port_view{argv[i + 2]};
      auto connect_port = parse_number(port_view);
      if (!connect_port.has_value()) {
        throw std::invalid_argument("invalid connect port");
      }

      address.port = static_cast<uint16_t>(*connect_port);
      config.addresses.push_back(std::move(address));
      i += 2;
    } else if (((view == "--mine-every") || view == "-m") &&
               i + 1 < argv.size()) {
      ++i;
      std::string_view seconds_view{argv[i]};
      auto mine_every = parse_number(seconds_view);
      if (!mine_every.has_value()) {
        throw std::invalid_argument("invalid mine-every number");
      }

      config.mine_every_seconds = *mine_every;
    }

    else if (((view == "--bootstrap-file") || view == "-b") &&
             i + 1 < argv.size()) {
      ++i;
      std::string_view bootstrap_file_path{argv[i]};
      if (!bootstrap_file_path.empty()) {
        file_bootstrap_path = bootstrap_file_path;
      }
    } else if (((view == "--rpc-port") || (view == "-R")) && i + 1 < argv.size()) {
      ++i;
      std::string_view rpc_port_view{argv[i]};
      auto rpc_port = parse_number(rpc_port_view);
      if (!rpc_port.has_value()) {
        throw std::invalid_argument("invalid rpc port");
      }

      config.rpc_port = static_cast<uint16_t>(*rpc_port);
    } else {
      throw std::invalid_argument("invalid command argument");
    }
  }
}

int main(int argc, char *argv[]) {
  forgechain::app::OrchestratorConfig config;
  forgechain::crypto::str BOOTSTRAP_FILE_PATH{};
  try {
    parse_args(std::span<char *>(argv, static_cast<size_t>(argc)), config,
               BOOTSTRAP_FILE_PATH);
  } catch (const std::exception &err) {
    std ::cerr << "Error: " << err.what() << std::endl;
    return 1;
  }

  auto bootstrap_container =
      forgechain::network::load_bootstrap_peers(BOOTSTRAP_FILE_PATH);
  if (bootstrap_container.has_value()) {
    if (bootstrap_container->empty() && !BOOTSTRAP_FILE_PATH.empty()) {
      std::cerr << "Warning: bootstrap file '" << BOOTSTRAP_FILE_PATH
                << "' contains no valid entries -- continuing without it"
                << std::endl;
    }
    config.addresses.insert(
        config.addresses.end(),
        std::make_move_iterator(bootstrap_container->begin()),
        std::make_move_iterator(bootstrap_container->end()));
  }

  else if (!BOOTSTRAP_FILE_PATH.empty()) {
    std::cerr << "Warning: could not open bootstrap file '"
              << BOOTSTRAP_FILE_PATH << "' -- continuing without it"
              << std::endl;
  }
  forgechain::app::Orchestrator orchestrator(config);

  g_orchestrator = &orchestrator;
  std::signal(SIGINT, handle_sigint);
  if (!orchestrator.start()) {
    std::cerr << "Failed to start node (port already in use?)" << std::endl;
    return 2;
  }
  orchestrator.run_command_loop();
  return 0;
}
