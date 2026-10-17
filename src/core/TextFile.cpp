#include "core/TextFile.hpp"

#include "core/TextUtf8.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace novel {
namespace {

constexpr std::string_view kAttempts = "已尝试 BOM / UTF-8 / GB18030";

constexpr std::string_view kReplacement = "\xEF\xBF\xBD";

// 容错阈值：最多允许 0.001% 的坏字节（至少 1 个）。
// 例如 6MB 文件最多 61 个，避免把二进制文件当小说打开。
std::size_t lossy_replacement_limit(std::size_t bytes) {
  return std::max<std::size_t>(1, bytes / 100000);
}

// 将非法 UTF-8 字节替换为 U+FFFD；超过 limit 立即失败，避免为垃圾文件
// 构造巨大的替换输出。
bool utf8_to_utf8_lossy(std::string_view bytes, std::size_t limit,
                        std::string& out, std::size_t& replacements) {
  out.clear();
  replacements = 0;
  out.reserve(bytes.size() + bytes.size() / 16 + 16);
  std::size_t i = 0;
  while (i < bytes.size()) {
    const auto c = static_cast<unsigned char>(bytes[i]);
    if (c < 0x80) {
      out.push_back(static_cast<char>(c));
      ++i;
      continue;
    }

    std::size_t len = 0;
    char32_t cp = 0;
    if ((c & 0xE0) == 0xC0) {
      len = 2;
      cp = static_cast<char32_t>(c & 0x1F);
    } else if ((c & 0xF0) == 0xE0) {
      len = 3;
      cp = static_cast<char32_t>(c & 0x0F);
    } else if ((c & 0xF8) == 0xF0) {
      len = 4;
      cp = static_cast<char32_t>(c & 0x07);
    } else {
      len = 0;
    }

    bool valid = len != 0 && i + len <= bytes.size();
    if (valid && len == 2) valid = cp >= 2;
    if (valid && len == 4) valid = cp <= 4;
    if (valid) {
      for (std::size_t k = 1; k < len; ++k) {
        const auto cc = static_cast<unsigned char>(bytes[i + k]);
        if ((cc & 0xC0) != 0x80) {
          valid = false;
          break;
        }
        cp = (cp << 6) | static_cast<char32_t>(cc & 0x3F);
      }
    }
    if (valid) {
      if (len == 3 && cp < 0x800) valid = false;
      if (len == 4 && cp < 0x10000) valid = false;
      if (cp > 0x10FFFF) valid = false;
      if (cp >= 0xD800 && cp <= 0xDFFF) valid = false;
    }

    if (!valid) {
      out.append(kReplacement.data(), kReplacement.size());
      if (++replacements > limit) return false;
      ++i;
      continue;
    }
    out.append(bytes.data() + i, len);
    i += len;
  }
  return true;
}

bool utf16_to_utf8(const char* data, std::size_t size, bool little_endian,
                   std::string& out) {
  if (size % 2 != 0) return false;
  out.clear();
  out.reserve(size);
  for (std::size_t i = 0; i < size; i += 2) {
    const auto b0 = static_cast<unsigned char>(data[i]);
    const auto b1 = static_cast<unsigned char>(data[i + 1]);
    char32_t unit = little_endian
 ? static_cast<char32_t>(b0 | (b1 << 8))
                        : static_cast<char32_t>((b0 << 8) | b1);
    if (unit >= 0xD800 && unit <= 0xDBFF) {
      if (i + 3 >= size) return false;
      const auto c0 = static_cast<unsigned char>(data[i + 2]);
      const auto c1 = static_cast<unsigned char>(data[i + 3]);
      const char32_t low = little_endian
 ? static_cast<char32_t>(c0 | (c1 << 8))
                               : static_cast<char32_t>((c0 << 8) | c1);
      if (low < 0xDC00 || low > 0xDFFF) return false;
      const char32_t cp =
          0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
      append_utf8(out, cp);
      i += 2;
    } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
      return false;
    } else {
      append_utf8(out, unit);
    }
  }
  return true;
}

#ifdef _WIN32
// 分块转换：整篇一次转换需要「原始字节 + UTF-16 中间缓冲 + UTF-8 输出」三份
// 同时驻留；按块处理后 UTF-16 缓冲只占一个常量大小，峰值明显下降。
constexpr std::size_t kGbChunkBytes = 1 << 17;

// 返回从 pos 起不超过 limit 字节的块末尾（不切断多字节字符）。
std::size_t gb18030_chunk_end(std::string_view bytes, std::size_t pos,
                              std::size_t limit) {
  std::size_t i = pos;
  while (i < bytes.size() && i - pos < limit) {
    const auto b = static_cast<unsigned char>(bytes[i]);
    std::size_t len = 1;
    if (b >= 0x81 && b <= 0xFE) {
      len = 2;
      if (i + 1 < bytes.size()) {
        const auto b2 = static_cast<unsigned char>(bytes[i + 1]);
        if (b2 >= 0x30 && b2 <= 0x39 && i + 3 < bytes.size()) {
          const auto b3 = static_cast<unsigned char>(bytes[i + 2]);
          const auto b4 = static_cast<unsigned char>(bytes[i + 3]);
          if (b3 >= 0x81 && b3 <= 0xFE && b4 >= 0x30 && b4 <= 0x39) len = 4;
        }
      }
    }
    i += len;
  }
  return i;
}

bool gb18030_to_utf8(std::string_view bytes, std::string& out, bool strict,
                     std::size_t max_replacements,
                     std::size_t& replacements);

// GB18030 合法序列长度；返回 0 表示当前位置不是合法序列。
std::size_t gb18030_sequence_length(std::string_view bytes, std::size_t i) {
  if (i >= bytes.size()) return 0;
  const auto b = static_cast<unsigned char>(bytes[i]);
  if (b <= 0x7F) return 1;
  if (b < 0x81 || b > 0xFE) return 0;
  if (i + 1 >= bytes.size()) return 0;
  const auto b2 = static_cast<unsigned char>(bytes[i + 1]);
  if (b2 >= 0x40 && b2 <= 0xFE && b2 != 0x7F) return 2;
  if (b2 >= 0x30 && b2 <= 0x39 && i + 3 < bytes.size()) {
    const auto b3 = static_cast<unsigned char>(bytes[i + 2]);
    const auto b4 = static_cast<unsigned char>(bytes[i + 3]);
    if (b3 >= 0x81 && b3 <= 0xFE && b4 >= 0x30 && b4 <= 0x39) return 4;
  }
  return 0;
}

// 严格解码失败时：保留合法片段，非法字节替换为 U+FFFD。
// 只对少量坏字节使用；每次替换前先检查上限，避免大段二进制被展开。
bool gb18030_to_utf8_lossy(std::string_view bytes, std::size_t limit,
                           std::string& out, std::size_t& replacements) {
  out.clear();
  replacements = 0;
  out.reserve(bytes.size() + bytes.size() / 2 + 16);
  std::size_t segment_start = 0;
  std::size_t i = 0;
  while (i < bytes.size()) {
    const std::size_t len = gb18030_sequence_length(bytes, i);
    if (len == 0) {
      if (i > segment_start) {
        std::string segment;
        std::size_t unused = 0;
        if (!gb18030_to_utf8(
                bytes.substr(segment_start, i - segment_start), segment, true,
                0, unused)) {
          return false;
        }
        out += segment;
      }
      out.append(kReplacement.data(), kReplacement.size());
      if (++replacements > limit) return false;
      ++i;
      segment_start = i;
      continue;
    }
    i += len;
  }
  if (segment_start < bytes.size()) {
    std::string segment;
    std::size_t unused = 0;
    if (!gb18030_to_utf8(bytes.substr(segment_start), segment, true, 0,
                         unused)) {
      return false;
    }
    out += segment;
  }
  return true;
}

bool gb18030_to_utf8(std::string_view bytes, std::string& out, bool strict,
                     std::size_t max_replacements,
                     std::size_t& replacements) {
  out.clear();
  replacements = 0;
  if (bytes.empty()) return true;
  if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return false;
  }
  // GB18030 单字节转 UTF-8 最多 1.5 倍（双字节汉字 -> 三字节），预留后无需再扩容。
  out.reserve(bytes.size() + bytes.size() / 2);
  // GB18030 每个字节最多产生 1 个 UTF-16 码元；UTF-8 每个码元最多 3 字节。
  // 直接按上界给缓冲，省掉每块两次 sizing 扫描（原实现的 4 次 Win32 调用降为 2 次）。
  std::wstring wide(kGbChunkBytes + 4, L'\0');
  const DWORD flags = strict ? MB_ERR_INVALID_CHARS : 0;
  std::size_t pos = 0;
  while (pos < bytes.size()) {
    const std::size_t end = gb18030_chunk_end(bytes, pos, kGbChunkBytes);
    if (end == pos) break;
    const auto chunk = static_cast<int>(end - pos);
    const int wide_len =
        MultiByteToWideChar(54936, flags, bytes.data() + pos, chunk,
                            wide.data(), static_cast<int>(wide.size()));
    if (wide_len <= 0 ||
        static_cast<std::size_t>(wide_len) > wide.size()) {
      return false;
    }
    if (!strict) {
      for (int i = 0; i < wide_len; ++i) {
        if (wide[static_cast<std::size_t>(i)] == 0xFFFD) {
          if (++replacements > max_replacements) return false;
        }
      }
    }
    const std::size_t old = out.size();
    const std::size_t max_utf8 = static_cast<std::size_t>(wide_len) * 3;
    out.resize(old + max_utf8);
    const int utf8_len = WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), wide_len, out.data() + old,
        static_cast<int>(max_utf8), nullptr, nullptr);
    if (utf8_len <= 0) return false;
    out.resize(old + static_cast<std::size_t>(utf8_len));
    pos = end;
  }
  return true;
}
#endif

TextLoadResult fail_with(std::string message) {
  TextLoadResult result;
  result.ok = false;
  result.error = std::move(message);
  return result;
}

std::string path_to_utf8(const std::filesystem::path& path) {
  const std::u8string value = path.u8string();
  return std::string(value.begin(), value.end());
}

}  // namespace

bool is_valid_utf8(std::string_view bytes) {
  std::size_t i = 0;
  const std::size_t n = bytes.size();
  while (i < n) {
    const auto c = static_cast<unsigned char>(bytes[i]);
    if (c < 0x80) {
      ++i;
      continue;
    }
    std::size_t len = 0;
    char32_t cp = 0;
    if ((c & 0xE0) == 0xC0) {
      len = 2;
      cp = static_cast<char32_t>(c & 0x1F);
      if (cp < 2) return false;
    } else if ((c & 0xF0) == 0xE0) {
      len = 3;
      cp = static_cast<char32_t>(c & 0x0F);
    } else if ((c & 0xF8) == 0xF0) {
      len = 4;
      cp = static_cast<char32_t>(c & 0x07);
      if (cp > 4) return false;
    } else {
      return false;
    }
    if (i + len > n) return false;
    for (std::size_t k = 1; k < len; ++k) {
      const auto cc = static_cast<unsigned char>(bytes[i + k]);
      if ((cc & 0xC0) != 0x80) return false;
      cp = (cp << 6) | static_cast<char32_t>(cc & 0x3F);
    }
    if (len == 3 && cp < 0x800) return false;
    if (len == 4 && cp < 0x10000) return false;
    if (cp > 0x10FFFF) return false;
    if (cp >= 0xD800 && cp <= 0xDFFF) return false;
    i += len;
  }
  return true;
}

void normalize_newlines(std::string& text) {
  if (text.find('\r') == std::string::npos) return;
  // 原地压缩：CRLF/CR 都只变短，不额外复制整篇文本。
  std::size_t write = 0;
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char c = text[i];
    if (c == '\r') {
      text[write++] = '\n';
      if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
    } else {
      text[write++] = c;
    }
  }
  text.resize(write);
}

namespace {

// `sink` 非空时表示入参的缓冲区可以接管，省掉一次整篇复制。
TextLoadResult decode_text_bytes_impl(std::string_view bytes, std::string* sink) {
  TextLoadResult result;

  const bool has_utf8_bom = bytes.size() >= 3 &&
                            static_cast<unsigned char>(bytes[0]) == 0xEF &&
                            static_cast<unsigned char>(bytes[1]) == 0xBB &&
                            static_cast<unsigned char>(bytes[2]) == 0xBF;
  const bool has_utf16le_bom =
      bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFF &&
      static_cast<unsigned char>(bytes[1]) == 0xFE;
  const bool has_utf16be_bom =
      bytes.size() >= 2 && static_cast<unsigned char>(bytes[0]) == 0xFE &&
      static_cast<unsigned char>(bytes[1]) == 0xFF;

  if (has_utf8_bom) {
    std::string_view body = bytes.substr(3);
    if (is_valid_utf8(body)) {
      if (sink != nullptr) {
        result.text = std::move(*sink);
        result.text.erase(0, 3);
      } else {
        result.text.assign(body);
      }
      result.encoding = "UTF-8 BOM";
    } else {
      std::size_t replacements = 0;
      std::string lossy;
      if (!utf8_to_utf8_lossy(body, lossy_replacement_limit(body.size()), lossy,
                              replacements)) {
        return fail_with(
            std::string("UTF-8 BOM 之后的内容不是合法 UTF-8，"
                        "且坏字节比例过高；") +
            std::string(kAttempts));
      }
      result.text = std::move(lossy);
      result.encoding = "UTF-8 BOM（容错）";
      result.warnings.push_back(
          "UTF-8 BOM 文件包含 " + std::to_string(replacements) +
          " 个无法解码的字节，已替换为 U+FFFD；请检查相关位置");
    }
  } else if (has_utf16le_bom) {
    if (!utf16_to_utf8(bytes.data() + 2, bytes.size() - 2, true, result.text)) {
      return fail_with(std::string("UTF-16LE 内容无法解码；") +
                       std::string(kAttempts));
    }
    result.encoding = "UTF-16LE";
  } else if (has_utf16be_bom) {
    if (!utf16_to_utf8(bytes.data() + 2, bytes.size() - 2, false,
                       result.text)) {
      return fail_with(std::string("UTF-16BE 内容无法解码；") +
                       std::string(kAttempts));
    }
    result.encoding = "UTF-16BE";
  } else if (is_valid_utf8(bytes)) {
    if (sink != nullptr) {
      result.text = std::move(*sink);
    } else {
      result.text.assign(bytes);
    }
    result.encoding = "UTF-8";
  } else {
#ifdef _WIN32
    std::size_t strict_replacements = 0;
    if (gb18030_to_utf8(bytes, result.text, true, 0, strict_replacements)) {
      // 正常 GB18030 是受支持编码，不产生警告；只有容错替换才提示。
      result.encoding = "GB18030";
    } else {
      const std::size_t limit = lossy_replacement_limit(bytes.size());
      std::string utf8_lossy;
      std::string gb_lossy;
      std::size_t utf8_replacements = 0;
      std::size_t gb_replacements = 0;
      const bool utf8_ok =
          utf8_to_utf8_lossy(bytes, limit, utf8_lossy, utf8_replacements);
      const bool gb_ok =
          gb18030_to_utf8_lossy(bytes, limit, gb_lossy, gb_replacements);
      if (utf8_ok && (!gb_ok || utf8_replacements <= gb_replacements)) {
        result.text = std::move(utf8_lossy);
        result.encoding = "UTF-8（容错）";
        result.warnings.push_back(
            "文件严格 UTF-8 / GB18030 解码失败，包含 " +
            std::to_string(utf8_replacements) +
            " 个无法解码的字节，已按 UTF-8 替换为 U+FFFD；"
            "请检查相关位置");
      } else if (gb_ok) {
        result.text = std::move(gb_lossy);
        result.encoding = "GB18030（容错）";
        result.warnings.push_back(
            "文件不是合法 UTF-8，且包含 " + std::to_string(gb_replacements) +
            " 个无法解码的 GB18030 字节，已替换为 U+FFFD；"
            "请检查相关位置");
      } else {
        return fail_with(
            std::string("无法识别文本编码；") + std::string(kAttempts) +
            "；坏字节比例超过 0.001%，未启用容错");
      }
    }
#else
    const std::size_t limit = lossy_replacement_limit(bytes.size());
    std::size_t replacements = 0;
    if (!utf8_to_utf8_lossy(bytes, limit, result.text, replacements)) {
      return fail_with(std::string("无法识别文本编码；") + std::string(kAttempts) +
                       "；坏字节比例超过 0.001%，未启用容错");
    }
    result.encoding = "UTF-8（容错）";
    result.warnings.push_back(
        "文件包含 " + std::to_string(replacements) +
        " 个无法解码的字节，已替换为 U+FFFD；请检查相关位置");
#endif
  }

  normalize_newlines(result.text);
  result.ok = true;
  return result;
}

}  // namespace

TextLoadResult decode_text_bytes(std::string_view bytes) {
  return decode_text_bytes_impl(bytes, nullptr);
}

TextLoadResult decode_text_bytes(std::string&& bytes) {
  return decode_text_bytes_impl(std::string_view(bytes), &bytes);
}

TextLoadResult load_text_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return fail_with("无法打开文件：" + path_to_utf8(path));
  }
  std::string bytes;
  in.seekg(0, std::ios::end);
  const std::streampos end = in.tellg();
  if (end > 0) {
    bytes.resize(static_cast<std::size_t>(end));
    in.seekg(0, std::ios::beg);
    if (in) {
      in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
      const std::streamsize got = in.gcount();
      bytes.resize(got > 0 ? static_cast<std::size_t>(got) : 0);
    } else {
      bytes.clear();
    }
  }
  if (bytes.empty()) {
    // tellg 失败（或空文件）时退回流式读取。
    in.clear();
    in.seekg(0, std::ios::beg);
    bytes.assign(std::istreambuf_iterator<char>(in),
                 std::istreambuf_iterator<char>());
  }
  if (in.bad()) {
    return fail_with("读取文件失败：" + path_to_utf8(path));
  }
  TextLoadResult result = decode_text_bytes(std::move(bytes));
  if (!result.ok) {
    result.error += "：" + path_to_utf8(path);
  }
  return result;
}

}  // namespace novel
