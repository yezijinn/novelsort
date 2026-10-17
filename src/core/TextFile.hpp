#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace novel {

struct TextLoadResult {
  bool ok = false;
  std::string text;  // UTF-8, no BOM, LF line endings
  std::string encoding;
  std::vector<std::string> warnings;
  std::string error;
};

// 解码字节：UTF-8 BOM、UTF-16LE BOM、UTF-16BE BOM、严格 UTF-8、
// GB18030（仅 Windows）。换行统一为 LF。
TextLoadResult decode_text_bytes(std::string_view bytes);
// 接管入参缓冲区（合法 UTF-8 时不再复制一份）。
TextLoadResult decode_text_bytes(std::string&& bytes);

TextLoadResult load_text_file(const std::filesystem::path& path);

bool is_valid_utf8(std::string_view bytes);

void normalize_newlines(std::string& text);

}  // namespace novel
