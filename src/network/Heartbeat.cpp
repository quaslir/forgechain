#include <chrono>
#include <atomic>
#include "network/Heartbeat.hpp"
namespace forgechain::network {


    Heartbeat::Heartbeat() : last_seen_(std::chrono::steady_clock::now()) {}

            Heartbeat& Heartbeat::operator=(Heartbeat&& other) noexcept {
                if(this != &other) {
                                        last_seen_ = other.last_seen_.load();
                }

                return *this;
            }
            Heartbeat::Heartbeat(Heartbeat&& other) noexcept : last_seen_(other.last_seen_.load()) {}

            void Heartbeat::touch() {
                last_seen_ = std::chrono::steady_clock::now();
            }

            [[nodiscard]] std::chrono::seconds Heartbeat::elapsed() const {
                return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - last_seen_.load());
            }

}
