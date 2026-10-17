#include "core/TextUtf8.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace novel {

std::size_t decode_cp(std::string_view s, std::size_t i, char32_t& cp) {
  if (i >= s.size()) {
    cp = 0;
    return 0;
  }
  const auto first = static_cast<unsigned char>(s[i]);
  if (first < 0x80) {
    cp = first;
    return 1;
  }

  std::size_t len = 0;
  char32_t value = 0;
  if ((first & 0xE0) == 0xC0) {
    len = 2;
    value = static_cast<char32_t>(first & 0x1F);
  } else if ((first & 0xF0) == 0xE0) {
    len = 3;
    value = static_cast<char32_t>(first & 0x0F);
  } else if ((first & 0xF8) == 0xF0) {
    len = 4;
    value = static_cast<char32_t>(first & 0x07);
  } else {
    cp = 0xFFFD;
    return 1;
  }

  if (i + len > s.size()) {
    cp = 0xFFFD;
    return 1;
  }
  for (std::size_t k = 1; k < len; ++k) {
    const auto cc = static_cast<unsigned char>(s[i + k]);
    if ((cc & 0xC0) != 0x80) {
      cp = 0xFFFD;
      return 1;
    }
    value = (value << 6) | static_cast<char32_t>(cc & 0x3F);
  }
  // 拒绝过短编码、超范围码点与代理区。
  if ((len == 2 && value < 0x80) || (len == 3 && value < 0x800) ||
      (len == 4 && value < 0x10000) || value > 0x10FFFF ||
      (value >= 0xD800 && value <= 0xDFFF)) {
    cp = 0xFFFD;
    return 1;
  }
  cp = value;
  return len;
}

void append_utf8(std::string& out, char32_t cp) {
  if (cp <= 0x7F) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

bool is_space_cp(char32_t cp) {
  switch (cp) {
    case U' ':
    case U'\t':
    case U'\r':
    case U'\n':
    case U'\v':
    case U'\f':
    case 0x00A0:  // 不换行空格
    case 0x1680:
    case 0x2000:
    case 0x2001:
    case 0x2002:
    case 0x2003:
    case 0x2004:
    case 0x2005:
    case 0x2006:
    case 0x2007:
    case 0x2008:
    case 0x2009:
    case 0x200A:
    case 0x200B:
    case 0x2028:
    case 0x2029:
    case 0x202F:
    case 0x205F:
    case 0x3000:  // 全角空格
    case 0xFEFF:  // BOM
      return true;
    default:
      return false;
  }
}

std::string_view trim_ws_view(std::string_view s) {
  std::size_t begin = 0;
  std::size_t end = s.size();
  while (begin < end) {
    char32_t cp = 0;
    const std::size_t len = decode_cp(s, begin, cp);
    if (!is_space_cp(cp)) break;
    begin += len;
  }
  while (end > begin) {
    // 回退到上一个码点首字节（跳过 UTF-8 续字节）。
    std::size_t pos = end - 1;
    while (pos > begin && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80) {
      --pos;
    }
    char32_t cp = 0;
    decode_cp(s, pos, cp);
    if (!is_space_cp(cp)) break;
    end = pos;
  }
  return s.substr(begin, end - begin);
}

std::string trim_ws(std::string_view s) { return std::string(trim_ws_view(s)); }

std::size_t cp_count(std::string_view s) {
  std::size_t count = 0;
  for (std::size_t i = 0; i < s.size();) {
    char32_t cp = 0;
    i += decode_cp(s, i, cp);
    ++count;
  }
  return count;
}

char32_t first_cp(std::string_view s) {
  char32_t cp = 0;
  if (s.empty()) return 0;
  decode_cp(s, 0, cp);
  return cp;
}

char32_t last_cp(std::string_view s) {
  if (s.empty()) return 0;
  std::size_t pos = s.size() - 1;
  while (pos > 0 && (static_cast<unsigned char>(s[pos]) & 0xC0) == 0x80) --pos;
  char32_t cp = 0;
  decode_cp(s, pos, cp);
  return cp;
}

std::vector<std::string> split_lines(std::string_view text) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '\n') continue;
    lines.emplace_back(text.substr(start, i - start));
    start = i + 1;
  }
  // 末尾没有换行时保留最后一段；有换行时不追加空行。
  if (start < text.size()) lines.emplace_back(text.substr(start));
  return lines;
}

std::vector<std::string_view> split_lines_view(std::string_view text) {
  std::vector<std::string_view> lines;
  std::size_t start = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '\n') continue;
    lines.push_back(text.substr(start, i - start));
    start = i + 1;
  }
  if (start < text.size()) lines.push_back(text.substr(start));
  return lines;
}

bool is_blank_line(std::string_view line) { return trim_ws_view(line).empty(); }

}  // namespace novel
