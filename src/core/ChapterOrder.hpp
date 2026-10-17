#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "core/Chapter.hpp"

namespace novel {

// moves are 1-based (from, to); applied in order, to is clamped to [1, n].
std::vector<Chapter> reorder_chapters(std::vector<Chapter> chapters,
                                      const std::vector<std::pair<int, int>>& moves);

// Renders chapters in the given order using the original line ranges.
// excluded_ranges 为已删除章节的源行区间，输出时跳过。
// text_exclusions 为源行内的无效片段，输出时只删除对应字节；为空时走旧快路径。
std::string render_txt(const std::string& original_utf8,
                       const std::vector<Chapter>& ordered,
                       const std::vector<std::size_t>& toc_lines,
                       const std::vector<LineRange>& excluded_ranges = {},
                       const std::vector<TextExclusion>& text_exclusions = {});

std::string directory_json(const std::string& source,
                           const std::string& encoding,
                           const std::vector<Chapter>& chapters,
                           const std::vector<std::size_t>& toc_lines,
                           const std::vector<std::string>& warnings);

}  // namespace novel
