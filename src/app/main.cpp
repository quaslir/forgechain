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
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
static constexpr const char *BOOTSTRAP_FILE_PATH{"bootstrap.conf"};

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
                forgechain::app::OrchestratorConfig &config) {

  for (size_t i = 1; i < argv.size(); i++) {
    std::string_view view{argv[i]};

    if (view == "--port" && i + 1 < argv.size()) {
      ++i;
      std::string_view port_view{argv[i]};
      uint16_t port{0};

      auto result = std::from_chars(port_view.data(),
                                    port_view.data() + port_view.size(), port);
      if (result.ec != std::errc{}) {
        throw std::invalid_argument("invalid port");
      }

      config.listen_port = port;
    } else if (view == "--connect" && i + 2 < argv.size()) {
      forgechain::network::PeerAddress address;
      address.host = std::string{argv[i + 1]};
      std::string_view port_view{argv[i + 2]};
      uint16_t port{0};

      auto result = std::from_chars(port_view.data(),
                                    port_view.data() + port_view.size(), port);
      if (result.ec != std::errc{}) {
        throw std::invalid_argument("invalid connect port");
      }

      address.port = port;
      config.addresses.push_back(std::move(address));
      i += 2;
    } else if (view == "--mine-every" && i + 1 < argv.size()) {
      ++i;
      std::string_view seconds_view{argv[i]};
      int seconds{0};

      auto result =
          std::from_chars(seconds_view.data(),
                          seconds_view.data() + seconds_view.size(), seconds);
      if (result.ec != std::errc{}) {
        throw std::invalid_argument("invalid mine-every number");
      }

      config.mine_every_seconds = seconds;
    }
  }
}

int main(int argc, char *argv[]) {
  forgechain::app::OrchestratorConfig config;

  auto bootstrap_container =
      forgechain::network::load_bootstrap_peers(BOOTSTRAP_FILE_PATH);
  if (bootstrap_container.has_value()) {
    config.addresses = std::move(*bootstrap_container);
  } else {
    forgechain::network::create_bootstrap_file(BOOTSTRAP_FILE_PATH);
  }
  try {
    parse_args(std::span<char *>(argv, static_cast<size_t>(argc)), config);
  } catch (const std::exception &err) {
    std::cerr << "Error: " << err.what() << std::endl;
    return 1;
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
