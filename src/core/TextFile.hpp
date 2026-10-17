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

// Decodes raw bytes: UTF-8 BOM -> UTF-16LE BOM -> UTF-16BE BOM -> strict UTF-8
// -> GB18030 (Windows only). Newlines are normalized to LF.
TextLoadResult decode_text_bytes(std::string_view bytes);
// 接管入参缓冲区（合法 UTF-8 时不再复制一份）。
TextLoadResult decode_text_bytes(std::string&& bytes);

TextLoadResult load_text_file(const std::filesystem::path& path);

bool is_valid_utf8(std::string_view bytes);

void normalize_newlines(std::string& text);

}  // namespace novel
