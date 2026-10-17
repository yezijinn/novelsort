#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "core/Chapter.hpp"
#include "core/TextUtf8.hpp"

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

// 以下三个函数已迁到 core/TextUtf8.hpp，此处仅为兼容旧调用保留声明。
// 共享文本辅助函数（渲染器也使用）。
std::vector<std::string> split_lines(std::string_view text);
// 零拷贝版本：视图指向 `text`，调用方必须保证其生命周期覆盖视图。
std::vector<std::string_view> split_lines_view(std::string_view text);
bool is_blank_line(std::string_view line);

}  // namespace novel
