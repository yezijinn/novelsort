#include "core/Json.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <string>

namespace novel {

void append_json_string(std::string& out, std::string_view text) {
  out.push_back('"');
  for (const char raw : text) {
    const auto c = static_cast<unsigned char>(raw);
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      default:
        if (c < 0x20) {
          static const char* const kHex = "0123456789abcdef";
          out += "\\u00";
          out.push_back(kHex[(c >> 4) & 0x0F]);
          out.push_back(kHex[c & 0x0F]);
        } else {
          out.push_back(raw);
        }
        break;
    }
  }
  out.push_back('"');
}

void append_json_number(std::string& out, long long value) {
  out += std::to_string(value);
}

void append_json_fixed(std::string& out, double value, int decimals) {
  if (!std::isfinite(value)) {
    out += "0";
    return;
  }
  std::array<char, 64> buffer{};
  const auto result =
      std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                    std::chars_format::fixed, decimals);
  if (result.ec == std::errc{}) {
    out.append(buffer.data(), result.ptr);
  } else {
    out += "0";
  }
}

}  // namespace novel
