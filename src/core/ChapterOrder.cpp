#include "core/ChapterOrder.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/ChapterParser.hpp"
#include "core/TextFile.hpp"

namespace novel {
namespace {

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

void append_number(std::string& out, long long value) {
  out += std::to_string(value);
}

// skip_lines 由 DirectoryEdit 维护为升序无重复，可直接二分。
bool has_skip_line(const Chapter& chapter, std::size_t line) {
  return std::binary_search(chapter.skip_lines.begin(),
                            chapter.skip_lines.end(), line);
}

bool is_indent_char(char c) { return c == ' ' || c == '\t'; }

// 行内无效片段：line -> 已排序合并的 [begin,end)。
using LineExclusions =
    std::unordered_map<std::size_t,
                       std::vector<std::pair<std::size_t, std::size_t>>>;

bool is_utf8_continuation(char c) {
  return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
}

// 起点落在多字节字符中间时向后收缩到字符首字节。
std::size_t shrink_to_char_begin(std::string_view line, std::size_t pos) {
  while (pos < line.size() && is_utf8_continuation(line[pos])) ++pos;
  return pos;
}

// 终点落在多字节字符中间时向前收缩到字符首字节。
std::size_t shrink_to_char_end(std::string_view line, std::size_t pos) {
  while (pos > 0 && pos < line.size() && is_utf8_continuation(line[pos])) --pos;
  return pos;
}

LineExclusions build_line_exclusions(
    const std::vector<std::string_view>& lines,
    const std::vector<TextExclusion>& text_exclusions) {
  LineExclusions result;
  for (const TextExclusion& exclusion : text_exclusions) {
    if (exclusion.begin_byte >= exclusion.end_byte) continue;
    if (exclusion.line >= lines.size()) continue;
    const std::size_t size = lines[exclusion.line].size();
    const std::size_t begin = std::min(exclusion.begin_byte, size);
    const std::size_t end = std::min(exclusion.end_byte, size);
    if (begin >= end) continue;
    result[exclusion.line].emplace_back(begin, end);
  }
  for (auto& entry : result) {
    auto& spans = entry.second;
    std::sort(spans.begin(), spans.end());
    std::vector<std::pair<std::size_t, std::size_t>> merged;
    merged.reserve(spans.size());
    for (const auto& span : spans) {
      if (!merged.empty() && span.first <= merged.back().second) {
        merged.back().second = std::max(merged.back().second, span.second);
      } else {
        merged.push_back(span);
      }
    }
    spans.swap(merged);
  }
  return result;
}

// 导出用的标题行：原文有 # 前缀时按 level 重写井号，否则原样输出 title。
std::string heading_text(const Chapter& chapter, std::string_view original) {
  std::size_t i = 0;
  while (i < original.size() && is_indent_char(original[i])) ++i;
  if (i >= original.size() || original[i] != '#') return chapter.title;
  const int level = chapter.level == 1 ? 1 : 2;
  std::string out(original.substr(0, i));
  out.append(static_cast<std::size_t>(level), '#');
  out.push_back(' ');
  out += chapter.title;
  return out;
}

}  // namespace

std::vector<Chapter> reorder_chapters(
    std::vector<Chapter> chapters,
    const std::vector<std::pair<int, int>>& moves) {
  const int count = static_cast<int>(chapters.size());
  if (count == 0) return chapters;

  for (const auto& move : moves) {
    int from = move.first;
    int to = move.second;
    if (from < 1 || from > count) continue;
    to = std::clamp(to, 1, count);
    Chapter chapter = std::move(chapters[static_cast<std::size_t>(from - 1)]);
    chapters.erase(chapters.begin() + (from - 1));
    chapters.insert(chapters.begin() + (to - 1), std::move(chapter));
  }

  for (std::size_t i = 0; i < chapters.size(); ++i) {
    chapters[i].index = static_cast<int>(i);
  }
  return chapters;
}

std::string render_txt(const std::string& original_utf8,
                       const std::vector<Chapter>& ordered,
                       const std::vector<std::size_t>& toc_lines,
                       const std::vector<LineRange>& excluded_ranges,
                       const std::vector<TextExclusion>& text_exclusions) {
  // 仅在含 CR 时复制并归一化，否则直接引用入参，避免整篇再复制一份。
  std::string normalized;
  std::string_view text = original_utf8;
  if (original_utf8.find('\r') != std::string::npos) {
    normalized = original_utf8;
    normalize_newlines(normalized);
    text = normalized;
  }
  const std::vector<std::string_view> lines = split_lines_view(text);

  std::vector<bool> in_toc(lines.size(), false);
  for (const std::size_t line : toc_lines) {
    if (line < in_toc.size()) in_toc[line] = true;
  }

  std::vector<bool> excluded(lines.size(), false);
  for (const LineRange& range : excluded_ranges) {
    const std::size_t begin = std::min(range.begin, lines.size());
    const std::size_t end = std::min(range.end, lines.size());
    for (std::size_t line = begin; line < end; ++line) excluded[line] = true;
  }

  std::vector<bool> covered(lines.size(), false);

  // 空参数时 map 为空，逐行走旧路径，输出与旧版本逐字节一致。
  const LineExclusions line_exclusions =
      text_exclusions.empty() ? LineExclusions{}
                              : build_line_exclusions(lines, text_exclusions);

  // 返回该行删除无效片段后的可见文本；整行被排除时返回 nullopt。
  std::string scratch;
  const auto filtered_line =
      [&](std::size_t source_line,
          std::string_view line) -> std::optional<std::string_view> {
    if (line_exclusions.empty()) return line;
    const auto it = line_exclusions.find(source_line);
    if (it == line_exclusions.end()) return line;
    const auto& spans = it->second;
    if (spans.size() == 1 && spans.front().first == 0 &&
        spans.front().second >= line.size()) {
      return std::nullopt;  // 整行排除：连空行也不留
    }
    scratch.clear();
    scratch.reserve(line.size());
    std::size_t cursor = 0;
    for (const auto& span : spans) {
      const std::size_t begin = shrink_to_char_begin(line, span.first);
      const std::size_t end = shrink_to_char_end(line, span.second);
      if (begin >= end || begin < cursor) continue;
      scratch.append(line.substr(cursor, begin - cursor));
      cursor = end;
    }
    scratch.append(line.substr(cursor));
    return std::string_view(scratch);
  };

  // 直接写入输出缓冲：章内跳过首尾空行，章与章之间恰好一个空行。
  std::string out;
  out.reserve(original_utf8.size() + 4096);

  for (const Chapter& chapter : ordered) {
    const std::size_t begin = std::min(chapter.start_line, lines.size());
    const std::size_t end = std::min(chapter.end_line, lines.size());

    const std::size_t mark = out.size();
    std::size_t last_nonblank = out.size();
    bool started = false;

    const auto emit = [&](std::string_view line) {
      const bool blank = is_blank_line(line);
      if (!started) {
        if (blank) return;
        if (!out.empty()) out.push_back('\n');
        started = true;
      }
      out.append(line);
      out.push_back('\n');
      if (!blank) last_nonblank = out.size();
    };

    if (chapter.inserted && !chapter.heading_line.empty()) {
      emit(chapter.heading_line);
    }
    // 划给本章、但不在 [start,end) 里的正文行：排在标题之后、主正文之前。
    // extra_body 为空时不走这段，保持旧路径逐字节一致。
    bool heading_pre_emitted = false;
    if (!chapter.extra_body.empty()) {
      const std::size_t title_line = chapter.start_line;
      if (!chapter.inserted && !chapter.synthetic && title_line < lines.size() &&
          title_line >= begin && title_line < end) {
        heading_pre_emitted = true;
        if (!excluded[title_line] && !in_toc[title_line]) {
          covered[title_line] = true;
          if (!has_skip_line(chapter, title_line) && !chapter.suppressed) {
            if (chapter.renamed || chapter.level_changed) {
              emit(heading_text(chapter, lines[title_line]));
            } else {
              const auto kept = filtered_line(title_line, lines[title_line]);
              if (kept) emit(*kept);
            }
          }
        }
      }
      for (const LineRange& range : chapter.extra_body) {
        const std::size_t rb = std::min(range.begin, lines.size());
        const std::size_t re = std::min(range.end, lines.size());
        for (std::size_t line = rb; line < re; ++line) {
          if (excluded[line] || in_toc[line] || covered[line]) continue;
          covered[line] = true;
          if (has_skip_line(chapter, line)) continue;
          const auto kept = filtered_line(line, lines[line]);
          if (!kept) continue;
          emit(*kept);
        }
      }
    }
    for (std::size_t line = begin; line < end; ++line) {
      if (excluded[line] || in_toc[line]) continue;
      if (heading_pre_emitted && line == chapter.start_line) continue;
      covered[line] = true;
      if (has_skip_line(chapter, line)) continue;

      const bool is_heading_line = !chapter.inserted && !chapter.synthetic &&
                                   line == chapter.start_line;
      if (is_heading_line) {
        if (chapter.suppressed) continue;
        if (chapter.renamed || chapter.level_changed) {
          emit(heading_text(chapter, lines[line]));
          continue;
        }
      }
      const auto kept = filtered_line(line, lines[line]);
      if (!kept) continue;
      emit(*kept);
    }
    if (!started) {
      out.resize(mark);
      continue;
    }
    out.resize(last_nonblank);
  }

  // 兜底：章节范围外的非空行也必须输出，不能静默丢字。
  // 已删除的源行属于有意丢弃，不参与这段检查。
  bool any_leftover = false;
  for (std::size_t line = 0; line < lines.size(); ++line) {
    if (in_toc[line] || covered[line] || excluded[line]) continue;
    if (is_blank_line(lines[line])) continue;
    const auto kept = filtered_line(line, lines[line]);
    if (!kept || is_blank_line(*kept)) continue;
    if (!any_leftover) {
      if (!out.empty()) out.push_back('\n');
      any_leftover = true;
    }
    out.append(*kept);
    out.push_back('\n');
  }
  return out;
}

std::string directory_json(const DirectoryJsonInput& input) {
  const std::vector<Chapter>& chapters = *input.chapters;
  const std::vector<std::size_t>& toc_lines = *input.toc_lines;
  const std::vector<std::string>& warnings = *input.warnings;

  std::string out;
  out += "{\"version\":1,\"source\":";
  append_json_string(out, input.source);
  out += ",\"encoding\":";
  append_json_string(out, input.encoding);
  out += ",\"chapters\":[";
  for (std::size_t i = 0; i < chapters.size(); ++i) {
    if (i != 0) out.push_back(',');
    const Chapter& chapter = chapters[i];
    out += "{\"index\":";
    append_number(out, static_cast<long long>(i) + 1);
    out += ",\"level\":";
    append_number(out, chapter.level);
    out += ",\"title\":";
    append_json_string(out, chapter.title);
    out += ",\"start_line\":";
    append_number(out, static_cast<long long>(chapter.start_line));
    out += ",\"end_line\":";
    append_number(out, static_cast<long long>(chapter.end_line));
    out += ",\"synthetic\":";
    out += chapter.synthetic ? "true" : "false";
    out.push_back('}');
  }
  out += "],\"toc_lines\":[";
  for (std::size_t i = 0; i < toc_lines.size(); ++i) {
    if (i != 0) out.push_back(',');
    append_number(out, static_cast<long long>(toc_lines[i]));
  }
  out += "],\"warnings\":[";
  for (std::size_t i = 0; i < warnings.size(); ++i) {
    if (i != 0) out.push_back(',');
    append_json_string(out, warnings[i]);
  }
  out += "]}";
  return out;
}

}  // namespace novel
