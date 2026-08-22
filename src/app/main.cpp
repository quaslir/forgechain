#include "app/Orchestrator.hpp"
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

forgechain::app::OrchestratorConfig parse_args(std::span<char *> argv) {
  forgechain::app::OrchestratorConfig config;

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
      config.connect_host = std::string{argv[i + 1]};
      std::string_view port_view{argv[i + 2]};
      uint16_t port{0};

      auto result = std::from_chars(port_view.data(),
                                    port_view.data() + port_view.size(), port);
      if (result.ec != std::errc{}) {
        throw std::invalid_argument("invalid connect port");
      }

      config.connect_port = port;
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

  return config;
}

int main(int argc, char *argv[]) {
  forgechain::app::OrchestratorConfig config;
  try {
    config = parse_args(std::span<char *>(argv, static_cast<size_t>(argc)));
  } catch (const std::exception &err) {
    std::cerr << "Error: " << err.what() << std::endl;
    return 1;
  }
  forgechain::app::Orchestrator orchestrator(config);
  if (!orchestrator.start()) {
    std::cerr << "Failed to start node (port already in use?)" << std::endl;
    return 2;
  }

  orchestrator.run_command_loop();
  return 0;
}
