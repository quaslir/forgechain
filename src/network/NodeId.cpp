#include "network/NodeId.hpp"
#include <cstdint>
#include <random>
namespace forgechain::network {
uint64_t generate_node_id() {
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<uint64_t> dist;
  return dist(gen);
}
} // namespace forgechain::network
