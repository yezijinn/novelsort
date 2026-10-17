#include "core/DirectoryEdit.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "core/ChapterParser.hpp"
#include "core/TextUtf8.hpp"

namespace novel {
namespace {

// 解析/格式化共用的数值上限。
constexpr long long kMaxChapterNumber = 1000000;  // 序号超过它不再当章节号
constexpr std::size_t kMaxNumberDigits = 6;       // 阿拉伯序号最大位数
constexpr std::size_t kMaxRomanChars = 8;         // 罗马数字最大字符数
constexpr int kMaxRomanValue = 3999;              // 罗马数字可表示的最大值

// 输入标题可能带 Markdown 井号：井号只保留在 heading_line，不进 title。
std::string strip_heading_marks(std::string_view text) {
  std::size_t i = 0;
  while (i < text.size() && text[i] == '#') ++i;
  if (i == 0) return std::string(text);
  while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
  return std::string(text.substr(i));
}

// ===== 中文数字 =====
int digit_of(char32_t cp) {
  if (cp >= U'0' && cp <= U'9') return static_cast<int>(cp - U'0');
  if (cp >= 0xFF10 && cp <= 0xFF19) return static_cast<int>(cp - 0xFF10);
  switch (cp) {
    case U'零':
    case U'〇':
      return 0;
    case U'一':
    case U'壹':
      return 1;
    case U'二':
    case U'两':
    case U'贰':
    case U'貳':
      return 2;
    case U'三':
    case U'叁':
    case U'參':
      return 3;
    case U'四':
    case U'肆':
      return 4;
    case U'五':
    case U'伍':
      return 5;
    case U'六':
    case U'陆':
    case U'陸':
      return 6;
    case U'七':
    case U'柒':
      return 7;
    case U'八':
    case U'捌':
      return 8;
    case U'九':
    case U'玖':
      return 9;
    default:
      return -1;
  }
}

long long unit_of(char32_t cp) {
  switch (cp) {
    case U'十':
    case U'拾':
      return 10;
    case U'百':
    case U'佰':
      return 100;
    case U'千':
    case U'仟':
      return 1000;
    case U'万':
    case U'萬':
      return 10000;
    case U'亿':
    case U'億':
      return 100000000;
    default:
      return 0;
  }
}

bool is_number_token_cp(char32_t cp) {
  return digit_of(cp) >= 0 || unit_of(cp) > 0;
}

bool is_skipped_unit(char32_t cp) {
  switch (cp) {
    case U'章':
    case U'节':
    case U'回':
    case U'话':
    case U'話':
    case U'幕':
    case U'集':
      return true;
    default:
      return false;
  }
}

bool parse_number_token(const std::vector<char32_t>& token, long long& value) {
  if (token.empty()) return false;
  bool has_unit = false;
  for (const char32_t cp : token) {
    if (digit_of(cp) < 0 && unit_of(cp) == 0) return false;
    if (unit_of(cp) > 0) has_unit = true;
  }

  if (!has_unit) {
    long long v = 0;
    for (const char32_t cp : token) {
      v = v * 10 + digit_of(cp);
      if (v > kMaxChapterNumber) return false;
    }
    value = v;
    return true;
  }

  long long total = 0;
  long long section = 0;
  long long number = 0;
  bool have_number = false;
  bool seen = false;
  for (const char32_t cp : token) {
    const int digit = digit_of(cp);
    if (digit >= 0) {
      number = digit;
      have_number = true;
      seen = true;
      continue;
    }
    const long long unit = unit_of(cp);
    if (unit >= 10000) {
      section += have_number ? number : 0;
      if (section == 0) section = 1;
      total += section * unit;
      section = 0;
      number = 0;
      have_number = false;
    } else {
      if (!have_number) number = 1;
      section += number * unit;
      number = 0;
      have_number = false;
    }
    seen = true;
  }
  total += section + number;
  if (!seen || total <= 0) return false;
  value = total;
  return true;
}

std::string chinese_under_10000(long long n) {
  static const char* const kDigits[] = {"零", "一", "二", "三", "四",
                                        "五", "六", "七", "八", "九"};
  const int d[4] = {static_cast<int>(n / 1000), static_cast<int>(n / 100 % 10),
                    static_cast<int>(n / 10 % 10), static_cast<int>(n % 10)};
  static const char* const kUnits[4] = {"千", "百", "十", ""};
  std::string out;
  bool pending_zero = false;
  for (int i = 0; i < 4; ++i) {
    if (d[i] == 0) {
      if (!out.empty()) pending_zero = true;
      continue;
    }
    if (pending_zero) {
      out += "零";
      pending_zero = false;
    }
    if (!(i == 2 && d[2] == 1 && out.empty())) out += kDigits[d[i]];
    out += kUnits[i];
  }
  return out;
}

std::string format_chinese(long long n) {
  if (n <= 0) return "零";
  if (n < 10000) return chinese_under_10000(n);
  if (n < 100000000) {
    const long long high = n / 10000;
    const long long low = n % 10000;
    std::string out = chinese_under_10000(high) + "万";
    if (low > 0) {
      if (low < 1000) out += "零";
      out += chinese_under_10000(low);
    }
    return out;
  }
  return std::to_string(n);  // 超出常见章节范围：退化为阿拉伯数字
}

// ===== 序号列解析与格式化 =====
bool is_arabic_digit(char32_t cp) {
  return (cp >= U'0' && cp <= U'9') || (cp >= 0xFF10 && cp <= 0xFF19);
}

bool is_chapter_unit(char32_t cp) {
  switch (cp) {
    case U'章':
    case U'节':
    case U'回':
    case U'话':
    case U'話':
    case U'幕':
    case U'集':
    case U'卷':
    case U'部':
    case U'篇':
      return true;
    default:
      return false;
  }
}

bool is_roman_char(char c) {
  switch (c) {
    case 'I':
    case 'V':
    case 'X':
    case 'L':
    case 'C':
    case 'D':
    case 'M':
    case 'i':
    case 'v':
    case 'x':
    case 'l':
    case 'c':
    case 'd':
    case 'm':
      return true;
    default:
      return false;
  }
}

long long roman_value(std::string_view s) {
  const auto digit = [](char c) -> int {
    switch (c) {
      case 'I':
      case 'i':
        return 1;
      case 'V':
      case 'v':
        return 5;
      case 'X':
      case 'x':
        return 10;
      case 'L':
      case 'l':
        return 50;
      case 'C':
      case 'c':
        return 100;
      case 'D':
      case 'd':
        return 500;
      case 'M':
      case 'm':
        return 1000;
      default:
        return 0;
    }
  };
  long long total = 0;
  int prev = 0;
  for (const char c : s) {
    const int cur = digit(c);
    if (cur == 0) return 0;
    total += (cur > prev) ? (cur - 2 * prev) : cur;
    prev = cur;
  }
  return total;
}

std::string to_roman(long long n, bool upper) {
  if (n <= 0 || n > kMaxRomanValue) return std::to_string(n);
  static const std::pair<int, const char*> kTable[] = {
      {1000, "M"}, {900, "CM"}, {500, "D"},  {400, "CD"}, {100, "C"},
      {90, "XC"},  {50, "L"},   {40, "XL"},  {10, "X"},   {9, "IX"},
      {5, "V"},    {4, "IV"},   {1, "I"}};
  std::string out;
  for (const auto& [value, text] : kTable) {
    while (n >= value) {
      out += text;
      n -= value;
    }
  }
  if (!upper) {
    for (char& c : out) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
  }
  return out;
}

enum class NumberStyle { kArabic, kChinese, kRoman };

// 序号列拆成「数字前原文 + 数字原文 + 数字后原文」；重排格式只需替换中间。
struct NumberParts {
  std::string head;
  std::string digits;
  std::string tail;
  std::string key;  // 计数器分组键（单位/关键词/标点）
  long long value = 0;
  int width = 0;  // 阿拉伯数字位数（保留 001 这类补零风格）
  NumberStyle style = NumberStyle::kArabic;
};

bool decode_digits(std::string_view text, long long& value, NumberStyle& style,
                   int& width) {
  if (text.empty()) return false;
  bool arabic = true;
  int count = 0;
  for (std::size_t i = 0; i < text.size();) {
    char32_t cp = 0;
    const std::size_t len = decode_cp(text, i, cp);
    if (!is_arabic_digit(cp)) {
      arabic = false;
      break;
    }
    ++count;
    i += len;
  }
  if (arabic) {
    long long v = 0;
    for (std::size_t i = 0; i < text.size();) {
      char32_t cp = 0;
      const std::size_t len = decode_cp(text, i, cp);
      v = v * 10 + digit_of(cp);
      if (v > kMaxChapterNumber) return false;
      i += len;
    }
    value = v;
    style = NumberStyle::kArabic;
    width = count;
    return true;
  }

  bool roman = true;
  for (const char c : text) {
    if (!is_roman_char(c)) {
      roman = false;
      break;
    }
  }
  if (roman && text.size() <= 12) {
    const long long v = roman_value(text);
    if (v > 0) {
      value = v;
      style = NumberStyle::kRoman;
      width = 0;
      return true;
    }
  }

  std::vector<char32_t> cps;
  cps.reserve(text.size());
  for (std::size_t i = 0; i < text.size();) {
    char32_t cp = 0;
    const std::size_t len = decode_cp(text, i, cp);
    cps.push_back(cp);
    i += len;
  }
  if (!parse_number_token(cps, value)) return false;
  style = NumberStyle::kChinese;
  width = 0;
  return true;
}

std::string cp_utf8(char32_t cp) {
  std::string out;
  append_utf8(out, cp);
  return out;
}

std::string ascii_lower_copy(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (const char c : s) {
    out.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

std::size_t plain_marker_len(std::string_view s, std::size_t pos) {
  if (pos >= s.size()) return 0;
  if (s.compare(pos, 3, "\xEF\xBC\x8E") == 0) return 3;  // ．
  if (s.compare(pos, 3, "\xE3\x80\x81") == 0) return 3;  // 、
  if (s[pos] == '.') return 1;
  return 0;
}

// 解析序号列。支持：第N单位、第(N)单位、英文关键词+编号、(1)、1.、I.。
// number 为空或不是可递增序号时返回 false（特殊章名/纯标题）。
bool parse_number_parts(std::string_view number, NumberParts& parts) {
  if (number.empty()) return false;

  // 1) 第 [（]数字[）] 单位
  if (number.compare(0, 3, "\xE7\xAC\xAC") == 0) {
    std::size_t i = 3;
    while (i < number.size()) {
      char32_t cp = 0;
      const std::size_t len = decode_cp(number, i, cp);
      if (!is_space_cp(cp)) break;
      i += len;
    }
    std::size_t digits_begin = i;
    std::size_t digits_end = i;
    char32_t open = 0;
    if (i < number.size()) decode_cp(number, i, open);
    if (open == U'(' || open == U'\uFF08') {
      const std::size_t open_len = (open == U'(') ? 1 : 3;
      const std::string close = (open == U'(') ? ")" : "\xEF\xBC\x89";
      const std::size_t close_pos = number.find(close, i + open_len);
      if (close_pos == std::string_view::npos) return false;
      digits_begin = i + open_len;
      digits_end = close_pos;
      i = close_pos + close.size();
    } else {
      while (i < number.size()) {
        char32_t cp = 0;
        const std::size_t len = decode_cp(number, i, cp);
        if (!is_number_token_cp(cp)) break;
        i += len;
      }
      digits_end = i;
    }
    while (i < number.size()) {
      char32_t cp = 0;
      const std::size_t len = decode_cp(number, i, cp);
      if (!is_space_cp(cp)) break;
      i += len;
    }
    if (i >= number.size()) return false;
    char32_t unit = 0;
    const std::size_t unit_len = decode_cp(number, i, unit);
    if (!is_chapter_unit(unit)) return false;
    i += unit_len;
    if (i != number.size()) return false;

    parts.head = std::string(number.substr(0, digits_begin));
    parts.digits =
        std::string(number.substr(digits_begin, digits_end - digits_begin));
    parts.tail = std::string(number.substr(digits_end));
    parts.key = cp_utf8(unit);
    return decode_digits(parts.digits, parts.value, parts.style, parts.width);
  }

  // 2) 英文关键词 + 编号（Chapter 1 / Ch. 2 / Volume 3）
  {
    const std::string low = ascii_lower_copy(number);
    static const char* const kWords[] = {"chapter", "episode", "volume",
                                         "part",    "book",    "ch.",
                                         "ep.",     "vol."};
    for (const char* word : kWords) {
      const std::size_t len = std::strlen(word);
      if (low.size() < len || low.compare(0, len, word) != 0) continue;
      if (low.size() > len &&
          std::isalpha(static_cast<unsigned char>(low[len]))) {
        continue;
      }
      std::size_t i = len;
      while (i < number.size()) {
        char32_t cp = 0;
        const std::size_t clen = decode_cp(number, i, cp);
        if (!is_space_cp(cp)) break;
        i += clen;
      }
      const std::size_t begin = i;
      while (i < number.size()) {
        char32_t cp = 0;
        const std::size_t clen = decode_cp(number, i, cp);
        const bool token = is_number_token_cp(cp) ||
                           (cp < 0x80 && is_roman_char(static_cast<char>(cp)));
        if (!token) break;
        i += clen;
      }
      if (i == begin) return false;
      parts.head = std::string(number.substr(0, begin));
      parts.digits = std::string(number.substr(begin, i - begin));
      parts.tail = std::string(number.substr(i));
      parts.key = std::string("en:") + word;
      return decode_digits(parts.digits, parts.value, parts.style, parts.width);
    }
  }

  // 3) 括号编号 (1)
  {
    char32_t open = 0;
    decode_cp(number, 0, open);
    if (open == U'(' || open == U'\uFF08') {
      const std::size_t open_len = (open == U'(') ? 1 : 3;
      const std::string close = (open == U'(') ? ")" : "\xEF\xBC\x89";
      const std::size_t close_pos = number.find(close, open_len);
      if (close_pos == std::string_view::npos) return false;
      const std::string_view inside =
          number.substr(open_len, close_pos - open_len);
      if (inside.empty()) return false;
      for (std::size_t k = 0; k < inside.size();) {
        char32_t cp = 0;
        const std::size_t len = decode_cp(inside, k, cp);
        if (!is_number_token_cp(cp)) return false;
        k += len;
      }
      parts.head = std::string(number.substr(0, open_len));
      parts.digits = std::string(inside);
      parts.tail = std::string(number.substr(close_pos));
      parts.key = "()";
      return decode_digits(parts.digits, parts.value, parts.style, parts.width);
    }
  }

  // 4) 纯数字 1. / 1、 / 1．
  {
    std::size_t i = 0;
    std::size_t digits = 0;
    while (i < number.size() && number[i] >= '0' && number[i] <= '9' &&
           digits < kMaxNumberDigits) {
      ++i;
      ++digits;
    }
    if (digits >= 1 && digits <= 4) {
      const std::size_t marker = plain_marker_len(number, i);
      if (marker > 0) {
        parts.head.clear();
        parts.digits = std::string(number.substr(0, i));
        parts.tail = std::string(number.substr(i));
        parts.key = "m:" + std::string(number.substr(i, marker));
        return decode_digits(parts.digits, parts.value, parts.style,
                             parts.width);
      }
    }
  }

  // 5) 罗马数字 I. / IV、
  {
    std::size_t j = 0;
    while (j < number.size() && is_roman_char(number[j]) && j < kMaxRomanChars) ++j;
    if (j > 0) {
      const std::size_t marker = plain_marker_len(number, j);
      if (marker > 0) {
        parts.head.clear();
        parts.digits = std::string(number.substr(0, j));
        parts.tail = std::string(number.substr(j));
        parts.key = "m:" + std::string(number.substr(j, marker));
        return decode_digits(parts.digits, parts.value, parts.style,
                             parts.width);
      }
    }
  }

  return false;
}

// 按 renumber 的参数格式化：chinese 用中文数字，否则阿拉伯（可补零）。
std::string format_parts_number(const NumberParts& parts, long long value,
                                bool chinese, int pad) {
  std::string digits;
  if (chinese) {
    digits = format_chinese(value);
  } else if (parts.style == NumberStyle::kRoman) {
    const bool upper = parts.digits.find_first_of(
                           "abcdefghijklmnopqrstuvwxyz") == std::string::npos;
    digits = to_roman(value, upper);
  } else {
    digits = std::to_string(value);
    if (pad > 0 && digits.size() < static_cast<std::size_t>(pad)) {
      digits.insert(0, static_cast<std::size_t>(pad) - digits.size(), '0');
    }
  }
  return parts.head + digits + parts.tail;
}

// 按章节自己的样式格式化（拆分时给新章 +1 用）。
std::string format_parts_like(const NumberParts& parts, long long value) {
  if (parts.style == NumberStyle::kChinese) {
    return parts.head + format_chinese(value) + parts.tail;
  }
  if (parts.style == NumberStyle::kRoman) {
    const bool upper = parts.digits.find_first_of(
                           "abcdefghijklmnopqrstuvwxyz") == std::string::npos;
    return parts.head + to_roman(value, upper) + parts.tail;
  }
  std::string digits = std::to_string(value);
  if (parts.width > 0 && digits.size() < static_cast<std::size_t>(parts.width)) {
    digits.insert(0, static_cast<std::size_t>(parts.width) - digits.size(),
                  '0');
  }
  return parts.head + digits + parts.tail;
}

// ===== index 重排与 skip_lines 去重 =====
void reindex(std::vector<Chapter>& chapters) {
  for (std::size_t i = 0; i < chapters.size(); ++i) {
    chapters[i].index = static_cast<int>(i);
  }
}

void dedupe_skip_lines(std::vector<std::size_t>& lines) {
  std::sort(lines.begin(), lines.end());
  lines.erase(std::unique(lines.begin(), lines.end()), lines.end());
}

// extra_body 必须按源行顺序排列：连续多次划转时，后插入的区间不能排到
// 先划入的区间之前；重叠/相邻区间合并，避免重复渲染。
void normalize_extra_body(std::vector<LineRange>& ranges) {
  ranges.erase(std::remove_if(ranges.begin(), ranges.end(),
                              [](const LineRange& r) { return r.end <= r.begin; }),
               ranges.end());
  std::sort(ranges.begin(), ranges.end(),
            [](const LineRange& a, const LineRange& b) {
              if (a.begin != b.begin) return a.begin < b.begin;
              return a.end < b.end;
            });
  std::vector<LineRange> merged;
  merged.reserve(ranges.size());
  for (const LineRange& r : ranges) {
    if (!merged.empty() && r.begin <= merged.back().end) {
      merged.back().end = std::max(merged.back().end, r.end);
    } else {
      merged.push_back(r);
    }
  }
  ranges.swap(merged);
}

// 序号/标题两列改完后重新拼完整标题。
void rebuild_title(Chapter& chapter) {
  chapter.title = chapter.number + chapter.separator + chapter.name;
}

// 插入章没有源标题行：标题变化后同步 heading_line，保留原有井号数量。
void sync_inserted_heading(Chapter& chapter) {
  if (!chapter.inserted) return;
  std::size_t hashes = 0;
  while (hashes < chapter.heading_line.size() &&
         chapter.heading_line[hashes] == '#') {
    ++hashes;
  }
  if (hashes > 0) {
    chapter.heading_line = std::string(hashes, '#') + " " + chapter.title;
  } else {
    chapter.heading_line = chapter.title;
  }
}

bool number_parts_of(const Chapter& chapter, NumberParts& parts) {
  if (!chapter.number.empty() && parse_number_parts(chapter.number, parts)) {
    return true;
  }
  Chapter probe;
  split_heading_display(chapter.title, probe);
  return !probe.number.empty() && parse_number_parts(probe.number, parts);
}

// 序号 +1：只改序号列与 title，不动 name；样式/单位/补零沿用本章。
bool bump_chapter_number(Chapter& chapter) {
  NumberParts parts;
  if (!number_parts_of(chapter, parts)) return false;
  chapter.number = format_parts_like(parts, parts.value + 1);
  rebuild_title(chapter);
  chapter.renamed = true;
  sync_inserted_heading(chapter);
  return true;
}

bool build_inserted(Chapter& chapter, std::string_view title, int level) {
  if (level != 1 && level != 2) return false;
  const std::string clean = trim_ws(title);
  if (clean.empty()) return false;
  chapter.level = level;
  chapter.heading_line = clean;
  chapter.title = strip_heading_marks(clean);
  split_heading_display(chapter.title, chapter);
  chapter.inserted = true;
  chapter.renamed = true;
  chapter.synthetic = false;
  return true;
}

// 插入章没有源标题行，改动层级时只能重写它自己的 heading_line。
// 原文以 # 开头才重写，保留缩进与井号风格。
std::string rewrite_hash_style(const std::string& line, int level) {
  std::size_t i = 0;
  while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
  if (i >= line.size() || line[i] != '#') return line;
  std::size_t hashes = i;
  while (hashes < line.size() && line[hashes] == '#') ++hashes;
  std::size_t text = hashes;
  while (text < line.size() && (line[text] == ' ' || line[text] == '\t')) ++text;
  std::string out = line.substr(0, i);
  out.append(static_cast<std::size_t>(level), '#');
  out.push_back(' ');
  out += line.substr(text);
  return out;
}

}  // namespace

bool rename_chapter(Chapter& ch, std::string_view title) {
  const std::string clean = trim_ws(title);
  if (clean.empty()) return false;
  ch.title = clean;
  ch.renamed = true;
  // title 是唯一真源：改名后重新拆两列，避免序号列与标题脱节。
  split_heading_display(ch.title, ch);
  sync_inserted_heading(ch);
  return true;
}

bool rename_chapter_parts(Chapter& ch, std::string_view number,
                          std::string_view name) {
  const std::string parts_number = trim_ws(number);
  const std::string parts_name = trim_ws(name);
  if (parts_number.empty() && parts_name.empty()) return false;

  std::string separator;
  if (!parts_number.empty() && !parts_name.empty()) {
    separator = ch.separator.empty() ? std::string(" ") : ch.separator;
  }
  ch.number = parts_number;
  ch.name = parts_name;
  ch.separator = separator;
  rebuild_title(ch);
  ch.renamed = true;

  Chapter probe;
  split_heading_display(ch.title, probe);
  ch.numbered = !parts_number.empty() && probe.numbered;
  sync_inserted_heading(ch);
  return true;
}

void set_excluded_from_numbering(Chapter& ch, bool excluded) {
  ch.excluded_from_numbering = excluded;
}

bool promote_to_numbering(std::vector<Chapter>& chapters, int index) {
  if (index < 0 || static_cast<std::size_t>(index) >= chapters.size()) {
    return false;
  }
  Chapter& target = chapters[static_cast<std::size_t>(index)];
  if (target.numbered) return false;

  // 优先占用下一同层编号章的空位，后续同层章顺延；
  // 若已经是最后一章，则沿用前一章样式 +1。
  int next = -1;
  for (std::size_t k = static_cast<std::size_t>(index) + 1;
       k < chapters.size(); ++k) {
    const Chapter& candidate = chapters[k];
    if (candidate.level == target.level && candidate.numbered &&
        !candidate.excluded_from_numbering) {
      next = static_cast<int>(k);
      break;
    }
  }

  std::string number;
  if (next >= 0) {
    number = chapters[static_cast<std::size_t>(next)].number;
  } else {
    for (int k = index - 1; k >= 0; --k) {
      const Chapter& candidate = chapters[static_cast<std::size_t>(k)];
      if (candidate.level != target.level || !candidate.numbered ||
          candidate.excluded_from_numbering) {
        continue;
      }
      NumberParts parts;
      if (number_parts_of(candidate, parts)) {
        number = format_parts_like(parts, parts.value + 1);
      }
      break;
    }
    if (number.empty()) number = "第1章";
  }

  std::string name = target.name.empty() ? target.title : target.name;
  if (name.empty()) name = "新章节";
  if (!rename_chapter_parts(target, number, name)) return false;
  target.excluded_from_numbering = false;
  if (target.inserted) sync_inserted_heading(target);

  if (next >= 0) {
    for (std::size_t k = static_cast<std::size_t>(next);
         k < chapters.size(); ++k) {
      Chapter& chapter = chapters[k];
      if (chapter.level != target.level || !chapter.numbered ||
          chapter.excluded_from_numbering) {
        continue;
      }
      bump_chapter_number(chapter);
    }
  }

  reindex(chapters);
  return true;
}

bool set_chapter_level(Chapter& ch, int level) {
  if (level != 1 && level != 2) return false;
  if (ch.level != level) ch.level_changed = true;
  ch.level = level;
  if (ch.inserted) ch.heading_line = rewrite_hash_style(ch.heading_line, level);
  return true;
}

bool merge_with_previous(std::vector<Chapter>& chapters, int index,
                         bool suppress_heading) {
  if (index <= 0 || static_cast<std::size_t>(index) >= chapters.size()) {
    return false;
  }
  Chapter& previous = chapters[static_cast<std::size_t>(index) - 1];
  // 相邻校验必须在搬走 current 之前，否则失败返回会留下被搬空的章。
  if (chapters[static_cast<std::size_t>(index)].start_line >
      previous.end_line) {
    return false;  // 两章不相邻
  }
  Chapter current = std::move(chapters[static_cast<std::size_t>(index)]);

  previous.end_line = std::max(previous.end_line, current.end_line);
  if (suppress_heading && !current.inserted && !current.synthetic) {
    previous.skip_lines.push_back(current.start_line);
  }
  if (!current.skip_lines.empty()) {
    previous.skip_lines.insert(previous.skip_lines.end(),
                               current.skip_lines.begin(),
                               current.skip_lines.end());
  }
  if (!current.extra_body.empty()) {
    previous.extra_body.insert(previous.extra_body.end(),
                               current.extra_body.begin(),
                               current.extra_body.end());
    normalize_extra_body(previous.extra_body);
  }
  dedupe_skip_lines(previous.skip_lines);

  chapters.erase(chapters.begin() + index);
  reindex(chapters);
  return true;
}

bool delete_chapter(std::vector<Chapter>& chapters, int index,
                    std::vector<LineRange>& excluded_ranges) {
  if (index < 0 || static_cast<std::size_t>(index) >= chapters.size()) {
    return false;
  }
  Chapter current = std::move(chapters[static_cast<std::size_t>(index)]);

  if (current.inserted) {
    // 插入章的源行属于邻章：不能剔除，否则会连带删掉邻章正文。
    if (index > 0) {
      Chapter& previous = chapters[static_cast<std::size_t>(index) - 1];
      previous.end_line = std::max(previous.end_line, current.end_line);
    }
  } else if (current.end_line > current.start_line) {
    excluded_ranges.push_back({current.start_line, current.end_line});
  }
  for (const LineRange& range : current.extra_body) {
    if (range.end > range.begin) excluded_ranges.push_back(range);
  }

  chapters.erase(chapters.begin() + index);
  reindex(chapters);
  return true;
}

bool split_chapter(std::vector<Chapter>& chapters, int index, std::size_t line,
                   std::string_view title, int level) {
  if (index < 0 || static_cast<std::size_t>(index) >= chapters.size()) {
    return false;
  }
  Chapter& origin = chapters[static_cast<std::size_t>(index)];
  if (!(origin.start_line < line && line < origin.end_line)) return false;

  Chapter fresh;
  if (!build_inserted(fresh, title, level)) return false;
  fresh.start_line = line;
  fresh.end_line = origin.end_line;
  origin.end_line = line;

  chapters.insert(chapters.begin() + index + 1, std::move(fresh));
  reindex(chapters);
  return true;
}

bool append_chapter(std::vector<Chapter>& chapters, std::size_t end_line,
                    std::string_view title, int level) {
  Chapter fresh;
  if (!build_inserted(fresh, title, level)) return false;
  fresh.start_line = end_line;
  fresh.end_line = end_line;
  chapters.push_back(std::move(fresh));
  reindex(chapters);
  return true;
}

bool renumber_chapters(std::vector<Chapter>& chapters, int start_number,
                       bool chinese, int pad) {
  if (start_number < 1 || pad < 0) return false;

  std::map<std::string, int> counters;
  for (Chapter& chapter : chapters) {
    if (chapter.level == 1) {
      // 卷级标题重置「章/节/话」这类二级计数器
      for (auto it = counters.begin(); it != counters.end();) {
        bool skip = false;
        const std::string& unit = it->first;
        for (std::size_t i = 0; i < unit.size();) {
          char32_t cp = 0;
          const std::size_t len = decode_cp(unit, i, cp);
          if (is_skipped_unit(cp)) skip = true;
          i += len;
        }
        if (skip) {
          it = counters.erase(it);
        } else {
          ++it;
        }
      }
    }

    if (!chapter.numbered || chapter.excluded_from_numbering) continue;
    NumberParts parts;
    if (!parse_number_parts(chapter.number, parts)) continue;

    auto insertion = counters.emplace(parts.key, start_number);
    const int number = insertion.first->second;
    ++insertion.first->second;

    chapter.number = format_parts_number(parts, number, chinese, pad);
    rebuild_title(chapter);
    chapter.renamed = true;
    sync_inserted_heading(chapter);
  }
  return true;
}

bool split_chapter_numbered(std::vector<Chapter>& chapters, int index,
                            std::size_t line, std::string_view number,
                            std::string_view name, int level) {
  if (level != 1 && level != 2) return false;
  if (index < 0 || static_cast<std::size_t>(index) >= chapters.size()) {
    return false;
  }
  Chapter& origin = chapters[static_cast<std::size_t>(index)];
  if (!(origin.start_line < line && line < origin.end_line)) return false;

  std::string fresh_number = trim_ws(number);
  const std::string fresh_name = trim_ws(name);
  std::string separator = origin.separator;
  bool fresh_numbered = false;

  NumberParts parts;
  const bool origin_numbered = origin.numbered &&
                               !origin.excluded_from_numbering &&
                               number_parts_of(origin, parts);
  if (origin_numbered) {
    // 序号跟随当前章样式递增：同单位、同样式、同分隔、同补零位数。
    fresh_number = format_parts_like(parts, parts.value + 1);
    fresh_numbered = true;
  } else if (fresh_number.empty() && fresh_name.empty()) {
    return false;
  }

  if (fresh_number.empty() || fresh_name.empty()) {
    separator.clear();
  } else if (separator.empty()) {
    separator = " ";
  }

  Chapter fresh;
  fresh.level = level;
  fresh.number = fresh_number;
  fresh.name = fresh_name;
  fresh.separator = separator;
  rebuild_title(fresh);
  if (!origin_numbered) {
    Chapter probe;
    split_heading_display(fresh.title, probe);
    fresh.numbered = !fresh_number.empty() && probe.numbered;
    fresh_numbered = fresh.numbered;
  } else {
    fresh.numbered = fresh_numbered;
  }

  // 插入章没有源标题行：沿用当前章的井号风格。
  std::size_t hashes = 0;
  while (hashes < origin.heading_line.size() &&
         origin.heading_line[hashes] == '#') {
    ++hashes;
  }
  if (hashes > 0) {
    fresh.heading_line = std::string(hashes, '#') + " " + fresh.title;
  } else {
    fresh.heading_line = fresh.title;
  }

  fresh.start_line = line;
  fresh.end_line = origin.end_line;
  fresh.inserted = true;
  fresh.renamed = true;
  fresh.excluded_from_numbering = false;

  origin.end_line = line;
  chapters.insert(chapters.begin() + index + 1, std::move(fresh));

  // 只有新章真正参与编号时才后移后续序号；新章无编号（如前言内拆分）时
  // 不能凭空改动后面的章节。
  if (fresh_numbered) {
    for (std::size_t k = static_cast<std::size_t>(index) + 2;
         k < chapters.size(); ++k) {
      Chapter& chapter = chapters[k];
      if (!chapter.numbered || chapter.excluded_from_numbering) continue;
      bump_chapter_number(chapter);
    }
  }
  reindex(chapters);
  return true;
}

bool move_body_to_previous(std::vector<Chapter>& chapters, int index) {
  if (index <= 0 || static_cast<std::size_t>(index) >= chapters.size()) {
    return false;
  }
  Chapter& current = chapters[static_cast<std::size_t>(index)];
  Chapter& previous = chapters[static_cast<std::size_t>(index) - 1];

  // 插入章的 start_line 本身就是正文首行，其余章的正文从 start_line+1 起。
  const std::size_t body_begin =
      (current.inserted || current.synthetic) ? current.start_line
                                               : current.start_line + 1;
  const bool has_body = current.end_line > body_begin ||
                        !current.extra_body.empty();
  if (!has_body) return false;

  previous.end_line = std::max(previous.end_line, current.end_line);
  if (!current.inserted && !current.synthetic) {
    previous.skip_lines.push_back(current.start_line);
  }
  for (const LineRange& range : current.extra_body) {
    if (range.end > range.begin) previous.extra_body.push_back(range);
  }
  normalize_extra_body(previous.extra_body);
  dedupe_skip_lines(previous.skip_lines);

  current.end_line = (current.inserted || current.synthetic)
 ? current.start_line
                         : current.start_line + 1;
  current.extra_body.clear();
  return true;
}

bool move_body_to_next(std::vector<Chapter>& chapters, int index) {
  if (index < 0 || static_cast<std::size_t>(index) + 1 >= chapters.size()) {
    return false;
  }
  Chapter& current = chapters[static_cast<std::size_t>(index)];
  Chapter& next = chapters[static_cast<std::size_t>(index) + 1];

  const std::size_t body_begin =
      (current.inserted || current.synthetic) ? current.start_line
                                               : current.start_line + 1;
  if (current.end_line <= body_begin && current.extra_body.empty()) {
    return false;
  }

  std::vector<LineRange> moved;
  if (current.end_line > body_begin) moved.push_back({body_begin, current.end_line});
  for (const LineRange& range : current.extra_body) {
    if (range.end > range.begin) moved.push_back(range);
  }
  if (moved.empty()) return false;

  next.extra_body.insert(next.extra_body.begin(), moved.begin(), moved.end());
  normalize_extra_body(next.extra_body);
  current.end_line = (current.inserted || current.synthetic)
 ? current.start_line
                         : current.start_line + 1;
  current.extra_body.clear();
  return true;
}

}  // namespace novel
