#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "core/Chapter.hpp"

namespace novel {

// moves 为 1 基（from, to）；按序应用，to 钳制在 [1, n]。
std::vector<Chapter> reorder_chapters(std::vector<Chapter> chapters,
                                      const std::vector<std::pair<int, int>>& moves);

// 按给定顺序渲染章节，沿用原始行区间。
// excluded_ranges 为已删除章节的源行区间，输出时跳过。
// text_exclusions 为源行内的无效片段，输出时只删除对应字节；为空时走旧快路径。
std::string render_txt(const std::string& original_utf8,
                       const std::vector<Chapter>& ordered,
                       const std::vector<std::size_t>& toc_lines,
                       const std::vector<LineRange>& excluded_ranges = {},
                       const std::vector<TextExclusion>& text_exclusions = {});

// 目录 JSON 的输入。用结构体代替连续的字符串参数，避免 source / encoding 传反。
struct DirectoryJsonInput {
  std::string source;
  std::string encoding;
  const std::vector<Chapter>* chapters = nullptr;
  const std::vector<std::size_t>* toc_lines = nullptr;
  const std::vector<std::string>* warnings = nullptr;
};

std::string directory_json(const DirectoryJsonInput& input);

}  // namespace novel
