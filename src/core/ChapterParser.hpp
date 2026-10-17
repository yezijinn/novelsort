#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "core/Chapter.hpp"

namespace novel {

struct ParseOptions {
  bool accept_unnumbered_titles = false;
  int max_title_chars = 80;
};

struct ParseResult {
  std::vector<Chapter> chapters;       // 阅读顺序，含合成章
  std::vector<std::size_t> toc_lines;  // 被判定为目录区的行号
  std::vector<std::string> warnings;
};

ParseResult parse_chapters(std::string_view utf8_text,
                           const ParseOptions& options = {});

// 把完整标题拆成 number/separator/name 三列并判定 numbered。
// 只写这三列与 numbered，不动 title；解析与编辑（改名/重编号/拆分）共用。
void split_heading_display(const std::string& full_title, Chapter& chapter);

// Shared text helpers (also used by the renderer).
std::vector<std::string> split_lines(std::string_view text);
// Zero-copy variant: views point into `text`, valid as long as it outlives them.
std::vector<std::string_view> split_lines_view(std::string_view text);
bool is_blank_line(std::string_view line);

}  // namespace novel
