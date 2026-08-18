#pragma once

#include <atomic>
#include <chrono>
namespace forgechain::network {
class Heartbeat {
public:
  Heartbeat();

  Heartbeat &operator=(const Heartbeat &) = delete;
  Heartbeat(const Heartbeat &) = delete;
  Heartbeat &operator=(Heartbeat &&) noexcept;
  Heartbeat(Heartbeat &&) noexcept;

  void touch();

  [[nodiscard]] std::chrono::seconds elapsed() const;

private:
  std::atomic<std::chrono::steady_clock::time_point> last_seen_;
};
} // namespace forgechain::network
