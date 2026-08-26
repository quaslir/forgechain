#pragma once
#include <charconv>
#include <optional>
#include <string_view>
#include <system_error>
namespace forgechain::app {
inline std::optional<int> parse_number(std::string_view str_number) {
  int number{0};

  auto result = std::from_chars(str_number.data(),
                                str_number.data() + str_number.size(), number);
  if (result.ec != std::errc{}) {
    return std::nullopt;
  }

  return number;
}
} // namespace forgechain::app
