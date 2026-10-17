#include "core/ChapterParser.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/TextFile.hpp"
#include "core/TextUtf8.hpp"

namespace novel {
namespace {

// 特殊章名与英文关键词：解析匹配（match_*）与两列拆分（split_heading_display）
// 共用同一份词表，改词表即同时影响两边，靠 display_matches_parse 用例锁住一致性。
constexpr std::string_view kSpecialWords[] = {
    "番外篇", "外传篇", "序章", "序言", "序文", "序幕", "楔子", "引子",
    "前言",   "尾声",   "终章", "后记", "番外", "外传", "前传", "后传",
    "特典",   "附录",   "跋"};

constexpr std::string_view kEnglishWords[] = {"chapter",  "episode",  "volume",
                                              "part",     "book",     "prologue",
                                              "epilogue", "ch.",      "ep.",
                                              "vol."};

// 英文数词：供数字记号消费。
constexpr std::string_view kNumberWords[] = {
    "one",  "two",  "three", "four",   "five",  "six",  "seven", "eight",
    "nine", "ten",  "eleven", "twelve", "twenty", "thirty", "hundred"};

// 与 kEnglishWords 一一对应的层级：1=卷/部/篇，2=章/节/话。
constexpr int kEnglishLevels[] = {2, 2, 1, 1, 1, 2, 2, 2, 2, 1};

constexpr char32_t kFullWidthSpace = 0x3000;

// 正文判定：标题后剩余文本超过这个码点数就当正文句子。
constexpr std::size_t kProseCpThreshold = 8;
// 标题字节上界：4 字节/码点，再加装饰符与分隔符的余量。
constexpr std::size_t kTitleByteSlack = 64;
// 数字记号最大位数，超过就不是章节号。
constexpr std::size_t kMaxNumberDigits = 6;
// 罗马数字最大字符数。
constexpr std::size_t kMaxRomanChars = 8;

bool is_ws_cp(char32_t cp) {
  return cp == U' ' || cp == U'\t' || cp == kFullWidthSpace;
}

bool is_separator_cp(char32_t cp) {
  if (is_ws_cp(cp)) return true;
  switch (cp) {
    case U':':
    case U'\uFF1A':  // ：
    case U'.':
    case U'\uFF0E':  // ．
    case U',':
    case U'\uFF0C':  // ，
    case U'\u3001':  // 、
    case U'\u2014':  // —
    case U'\u2013':  // –
    case U'\u00B7':  // ·
    case U'\u30FB':  // ・
    case U'-':
      return true;
    default:
      return false;
  }
}

std::string collapse_ws(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  bool pending_space = false;
  std::size_t i = 0;
  while (i < s.size()) {
    char32_t cp = 0;
    const std::size_t len = decode_cp(s, i, cp);
    if (is_ws_cp(cp)) {
      pending_space = !out.empty();
    } else {
      if (pending_space) out.push_back(' ');
      pending_space = false;
      out.append(s.substr(i, len));
    }
    i += len;
  }
  return out;
}

// 码点向量：剥离装饰符需逐码点改写，无法在 string_view 上原地完成。
std::vector<char32_t> to_cps(std::string_view s) {
  std::vector<char32_t> out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size();) {
    char32_t cp = 0;
    const std::size_t len = decode_cp(s, i, cp);
    out.push_back(cp);
    i += len;
  }
  return out;
}

std::string from_cps(const std::vector<char32_t>& cps) {
  std::string out;
  out.reserve(cps.size() * 3);
  for (char32_t cp : cps) append_utf8(out, cp);
  return out;
}

bool is_letter_or_han(char32_t cp) {
  if ((cp >= U'a' && cp <= U'z') || (cp >= U'A' && cp <= U'Z')) return true;
  if (cp >= 0x4E00 && cp <= 0x9FFF) return true;   // CJK unified
  if (cp >= 0x3400 && cp <= 0x4DBF) return true;   // CJK ext A
  if (cp >= 0xF900 && cp <= 0xFAFF) return true;   // CJK compatibility
  if (cp >= 0xFF21 && cp <= 0xFF3A) return true;   // fullwidth A-Z
  if (cp >= 0xFF41 && cp <= 0xFF5A) return true;   // fullwidth a-z
  if (cp >= 0x3040 && cp <= 0x30FF) return true;   // kana
  return false;
}

bool has_letter_or_han(std::string_view s) {
  std::size_t i = 0;
  while (i < s.size()) {
    char32_t cp = 0;
    i += decode_cp(s, i, cp);
    if (is_letter_or_han(cp)) return true;
  }
  return false;
}

bool is_number_cp(char32_t cp) {
  if (cp >= U'0' && cp <= U'9') return true;
  if (cp >= 0xFF10 && cp <= 0xFF19) return true;
  switch (cp) {
    case U'零':
    case U'〇':
    case U'一':
    case U'二':
    case U'三':
    case U'四':
    case U'五':
    case U'六':
    case U'七':
    case U'八':
    case U'九':
    case U'十':
    case U'百':
    case U'千':
    case U'万':
    case U'萬':
    case U'亿':
    case U'億':
    case U'两':
    case U'壹':
    case U'贰':
    case U'貳':
    case U'叁':
    case U'參':
    case U'肆':
    case U'伍':
    case U'陆':
    case U'陸':
    case U'柒':
    case U'捌':
    case U'玖':
    case U'拾':
    case U'佰':
    case U'仟':
      return true;
    default:
      return false;
  }
}

std::size_t leading_sep_len(std::string_view s) {
  std::size_t i = 0;
  while (i < s.size()) {
    char32_t cp = 0;
    const std::size_t len = decode_cp(s, i, cp);
    if (!is_separator_cp(cp)) break;
    i += len;
  }
  return i;
}

bool is_sentence_end(char32_t cp) {
  switch (cp) {
    case U'\u3002':  // 。
    case U'\uFF01':  // ！
    case U'\uFF1F':  // ？
    case U'\u2026':  // …
    case U'!':
    case U'?':
    case U'\uFF1B':  // ；
    case U';':
    case U'\uFF0E':  // ．
    case U'\u2500':  // ─
    case U'\uFF5E':  // ～
    case U'\u2014':  // —
      return true;
    default:
      return false;
  }
}

// 句末标点：其后还有内容时，标题应判为正文。
// `！` / `？` / `…` 刻意排除：真实标题常用，不能据此判为正文。
// (``第270章 官宣！（全书完）``, ``第139章 劝架！劝人吵架！``).
bool is_hard_sentence_end(char32_t cp) {
  switch (cp) {
    case U'\u3002':  // 。
    case U'\uFF1B':  // ；
    case U';':
      return true;
    default:
      return false;
  }
}

bool is_wrapped_group(std::string_view s) {
  if (s.size() < 6) return false;
  const char32_t open = first_cp(s);
  const char32_t close = last_cp(s);
  return (open == U'\uFF08' && close == U'\uFF09') ||  // （）
         (open == U'(' && close == U')') ||
         (open == U'\u3010' && close == U'\u3011') ||  // 【】
         (open == U'\u300A' && close == U'\u300B') ||  // 《》
         (open == U'\u300C' && close == U'\u300D') ||  // 「」
         (open == U'\u300E' && close == U'\u300F');    // 『』
}

bool has_embedded_sentence_end(std::string_view s) {
  std::size_t i = 0;
  while (i < s.size()) {
    char32_t cp = 0;
    i += decode_cp(s, i, cp);
    if (!is_hard_sentence_end(cp)) continue;

    // 连续句末标点视为一个边界；行尾结束不构成句中句号。
    std::string_view rest = s.substr(i);
    std::size_t j = 0;
    while (j < rest.size()) {
      char32_t next = 0;
      const std::size_t len = decode_cp(rest, j, next);
      if (!is_hard_sentence_end(next)) break;
      j += len;
    }
    const std::string tail = trim_ws(rest.substr(j));
    if (tail.empty()) continue;
    // 其后只允许收尾标点或尾注（如「（全书完）」）；
    // 否则视为第二句。
    if (!has_letter_or_han(tail)) continue;
    if (is_wrapped_group(tail)) continue;
    return true;
  }
  return false;
}

bool wrapped_in_quotes(std::string_view s) {
  if (s.size() < 6) return false;
  const std::string_view head = s.substr(0, 3);
  const std::string_view tail = s.substr(s.size() - 3);
  if (head == "\xE3\x80\x8C" && tail == "\xE3\x80\x8D") return true;  // 「」
  if (head == "\xE3\x80\x8E" && tail == "\xE3\x80\x8F") return true;  // 『』
  return false;
}

bool rest_looks_like_sentence(std::string_view rest, bool check_length) {
  if (rest.empty()) return false;
  if (is_sentence_end(last_cp(rest))) return true;
  if (rest.find("\xEF\xBC\x8C") != std::string_view::npos) return true;  // ，
  if (rest.find(',') != std::string_view::npos) return true;
  if (check_length && !wrapped_in_quotes(rest) && cp_count(rest) > kProseCpThreshold) {
    return true;
  }
  return false;
}

std::string strip_decorations(std::string_view raw) {
  std::vector<char32_t> cps = to_cps(raw);
  auto trim_cps = [](std::vector<char32_t>& v) {
    while (!v.empty() && is_ws_cp(v.front())) v.erase(v.begin());
    while (!v.empty() && is_ws_cp(v.back())) v.pop_back();
  };
  trim_cps(cps);

  bool changed = true;
  while (changed) {
    changed = false;
    trim_cps(cps);
    if (cps.size() >= 3) {
      static const char32_t opens[] = {0x3010, 0x3016, 0x3014,
                                       0x3008, 0x300A, 0xFF08};
      static const char32_t closes[] = {0x3011, 0x3017, 0x3015,
                                        0x3009, 0x300B, 0xFF09};
      for (std::size_t k = 0; k < 6; ++k) {
        if (cps.front() == opens[k] && cps.back() == closes[k]) {
          cps.erase(cps.begin());
          cps.pop_back();
          changed = true;
          break;
        }
      }
      if (changed) continue;

      const char32_t c = cps.front();
      const bool symmetric =
          (c == U'=' || c == U'*' || c == U'\u2606' || c == U'\u2605' ||
           c == U'\u2014');
      if (symmetric && cps.back() == c) {
        std::size_t lead = 0;
        while (lead < cps.size() && cps[lead] == c) ++lead;
        std::size_t tail = 0;
        while (tail < cps.size() && cps[cps.size() - 1 - tail] == c) ++tail;
        if (lead == tail && cps.size() > 2 * lead) {
          cps.erase(cps.end() - static_cast<std::ptrdiff_t>(tail), cps.end());
          cps.erase(cps.begin(),
                    cps.begin() + static_cast<std::ptrdiff_t>(lead));
          changed = true;
        }
      }
    }
  }
  return from_cps(cps);
}

struct Match {
  bool ok = false;
  int level = 2;
  int kind = 0;
  std::string title;
};

enum HeadingKind {
  kMarkdown = 0,
  kNumbered = 1,
  kSpecial = 2,
  kEnglish = 3,
  kBracket = 4,
  kPlainNumber = 5,
  kUnnumbered = 6,
};

Match make_match(int level, const std::string& title, int kind) {
  Match m;
  m.ok = true;
  m.level = level;
  m.kind = kind;
  m.title = title;
  return m;
}

bool is_arabic_digit_cp(char32_t cp) {
  return (cp >= U'0' && cp <= U'9') || (cp >= 0xFF10 && cp <= 0xFF19);
}

// 「第 [（]数字[）] 单位」的扫描结果；number 列与匹配守卫共用同一套判定。
struct ChineseNumberedScan {
  bool ok = false;
  std::size_t unit_end = 0;  // 单位之后的字节偏移（number 列右边界）
  bool arabic = false;       // 数字部分是否全部为阿拉伯数字
  int level = 2;
};

ChineseNumberedScan scan_chinese_numbered(std::string_view s) {
  ChineseNumberedScan scan;
  if (s.compare(0, 3, "\xE7\xAC\xAC") != 0) return scan;  // 第
  std::size_t i = 3;
  i += leading_sep_len(s.substr(i));

  const bool bracketed = [&] {
    const char32_t first = (i < s.size()) ? first_cp(s.substr(i)) : 0;
    return first == U'(' || first == U'\uFF08';
  }();
  bool arabic = true;
  if (bracketed) {
    // 第(1)章 / 第（一）章
    const char32_t open = first_cp(s.substr(i));
    const std::size_t open_len = (open == U'(') ? 1 : 3;
    const std::string close = (open == U'(') ? ")" : "\xEF\xBC\x89";
    const std::size_t close_pos = s.find(close, i + open_len);
    if (close_pos == std::string_view::npos) return scan;
    const std::string_view inside =
        s.substr(i + open_len, close_pos - (i + open_len));
    if (inside.empty()) return scan;
    std::size_t k = 0;
    while (k < inside.size()) {
      char32_t cp = 0;
      const std::size_t len = decode_cp(inside, k, cp);
      if (!is_number_cp(cp)) return scan;
      if (!is_arabic_digit_cp(cp)) arabic = false;
      k += len;
    }
    i = close_pos + close.size();
    i += leading_sep_len(s.substr(i));
  } else {
    const std::size_t number_start = i;
    while (i < s.size()) {
      char32_t cp = 0;
      const std::size_t len = decode_cp(s, i, cp);
      if (!is_number_cp(cp)) break;
      if (!is_arabic_digit_cp(cp)) arabic = false;
      i += len;
    }
    if (i == number_start) return scan;
    // 数字与单位之间允许空白。
    i += leading_sep_len(s.substr(i));
  }
  if (i >= s.size()) return scan;

  char32_t unit = 0;
  const std::size_t unit_len = decode_cp(s, i, unit);
  int level = 0;
  switch (unit) {
    case U'章':
    case U'节':
    case U'回':
    case U'话':
    case U'話':
    case U'幕':
    case U'集':
      level = 2;
      break;
    case U'卷':
    case U'部':
    case U'篇':
      level = 1;
      break;
    default:
      return scan;
  }

  scan.ok = true;
  scan.unit_end = i + unit_len;
  scan.arabic = arabic;
  scan.level = level;
  return scan;
}

// 无空格标题的放行条件：阿拉伯编号 + 余下以 ！/？ 收尾且不含句读。
// 例如「第1章鞋跟先生！」是标题，「第3章的内容我看过了，……」不是。
// 中文数字编号不放行，保持「第一章真好看！」这类段落假阳性为原有判定。
bool is_no_space_heading_tail(std::string_view tail) {
  if (tail.empty() || !has_letter_or_han(tail)) return false;
  const char32_t last = last_cp(tail);
  if (last != U'\uFF01' && last != U'\uFF1F' && last != U'!' && last != U'?') {
    return false;
  }
  static const char* const kForbidden[] = {
      "\xE3\x80\x82",  // 。
      "\xEF\xBC\x8C",  // ，
      "\xE3\x80\x81",  // 、
      "\xEF\xBC\x9B",  // ；
      "\xEF\xBC\x9A",  // ：
  };
  for (const char* mark : kForbidden) {
    if (tail.find(mark) != std::string_view::npos) return false;
  }
  return true;
}

Match match_numbered(const std::string& core) {
  const ChineseNumberedScan scan = scan_chinese_numbered(core);
  if (!scan.ok) return {};

  const std::string_view rest(core.data() + scan.unit_end,
                             core.size() - scan.unit_end);
  const std::size_t sep = leading_sep_len(rest);
  if (sep == 0) {
    const std::string remainder = trim_ws(rest);
    if (!remainder.empty() &&
        rest_looks_like_sentence(remainder, true)) {
      if (!(scan.arabic && is_no_space_heading_tail(remainder))) return {};
    }
  }
  return make_match(scan.level, core, kNumbered);
}

Match match_special(const std::string& core) {
  for (const std::string_view word : kSpecialWords) {
    if (core.compare(0, word.size(), word) != 0) continue;
    std::size_t i = word.size();
    while (i < core.size()) {
      char32_t cp = 0;
      const std::size_t len = decode_cp(core, i, cp);
      if (!is_number_cp(cp)) break;
      i += len;
    }
    const std::string_view rest(core.data() + i, core.size() - i);
    const std::size_t sep = leading_sep_len(rest);
    // 特殊章名后必须跟分隔符或行尾，避免把正文行当标题。
    if (sep == 0 && !trim_ws(rest).empty()) return {};
    return make_match(2, core, kSpecial);
  }
  return {};
}

std::string ascii_lower(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) {
    out.push_back(static_cast<char>(
        std::tolower(static_cast<unsigned char>(c))));
  }
  return out;
}

bool is_roman_letter(char c) {
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

// 消费一个可选数字记号：阿拉伯数字、罗马数字或英文数词。
std::size_t consume_number_token(const std::string& s, std::size_t pos) {
  if (pos >= s.size()) return 0;
  std::size_t i = pos;
  std::size_t count = 0;
  while (i < s.size()) {
    char32_t cp = 0;
    const std::size_t len = decode_cp(s, i, cp);
    if (!(cp >= U'0' && cp <= U'9') && !(cp >= 0xFF10 && cp <= 0xFF19)) break;
    i += len;
    ++count;
  }
  if (count > 0 && count <= kMaxNumberDigits) return i - pos;

  // 全角罗马数字。
  {
    std::size_t j = pos;
    std::size_t rl = 0;
    while (j < s.size() && rl < kMaxRomanChars) {
      char32_t cp = 0;
      const std::size_t len = decode_cp(s, j, cp);
      if (!((cp >= 0x2160 && cp <= 0x2169) ||
            (cp >= 0x2170 && cp <= 0x2179))) {
        break;
      }
      j += len;
      ++rl;
    }
    if (rl > 0) return j - pos;
  }

  // 半角罗马数字。
  {
    std::size_t j = pos;
    while (j < s.size() && is_roman_letter(s[j]) && j - pos < kMaxRomanChars) ++j;
    if (j > pos && (j >= s.size() || !std::isalpha(
                                        static_cast<unsigned char>(s[j])))) {
      return j - pos;
    }
  }

  for (const std::string_view word : kNumberWords) {
    const std::size_t len = word.size();
    if (s.compare(pos, len, word) != 0) continue;
    const std::size_t after = pos + len;
    if (after < s.size() &&
        std::isalpha(static_cast<unsigned char>(s[after]))) {
      continue;
    }
    return len;
  }
  return 0;
}

Match match_english(const std::string& core) {
  const std::string low = ascii_lower(core);
  for (std::size_t k = 0; k < std::size(kEnglishWords); ++k) {
    const std::string_view word = kEnglishWords[k];
    const std::size_t len = word.size();
    if (low.size() < len) continue;
    if (low.compare(0, len, word) != 0) continue;
    if (low.size() > len &&
        std::isalpha(static_cast<unsigned char>(low[len]))) {
      continue;
    }
    std::size_t i = len;
    i += leading_sep_len(std::string_view(low.data() + i, low.size() - i));
    const std::size_t num = consume_number_token(core, i);
    i += num;
    const std::string_view rest(core.data() + i, core.size() - i);
    const std::size_t sep = leading_sep_len(rest);
    if (sep == 0) {
      const std::string remainder = trim_ws(rest);
      if (!remainder.empty() &&
          rest_looks_like_sentence(remainder, true)) {
        return {};
      }
    }
    return make_match(kEnglishLevels[k], core, kEnglish);
  }
  return {};
}

Match match_bracket(const std::string& core) {
  const char32_t open = first_cp(core);
  if (open != U'(' && open != U'\uFF08') return {};
  const std::size_t open_len = (open == U'(') ? 1 : 3;
  const std::string close = (open == U'(') ? ")" : "\xEF\xBC\x89";  // ）
  const std::size_t close_pos = core.find(close, open_len);
  if (close_pos == std::string::npos) return {};
  const std::string inside = core.substr(open_len, close_pos - open_len);
  if (inside.empty()) return {};
  {
    std::size_t i = 0;
    while (i < inside.size()) {
      char32_t cp = 0;
      const std::size_t len = decode_cp(inside, i, cp);
      if (!is_number_cp(cp)) return {};
      i += len;
    }
  }
  const std::size_t after = close_pos + close.size();
  const std::string_view rest(core.data() + after, core.size() - after);
  const std::size_t sep = leading_sep_len(rest);
  const std::string remainder = trim_ws(rest);
  if (remainder.empty()) return {};
  if (sep == 0 &&
      rest_looks_like_sentence(remainder, true)) {
    return {};
  }
  return make_match(2, core, kBracket);
}

Match match_plain_number(const std::string& core) {
  std::size_t i = 0;
  std::size_t digits = 0;
  while (i < core.size() && core[i] >= '0' && core[i] <= '9' && digits < kMaxNumberDigits) {
    ++i;
    ++digits;
  }
  bool matched = false;
  std::size_t marker_len = 0;
  if (digits >= 1 && digits <= 4 && i < core.size()) {
    if (core.compare(i, 3, "\xEF\xBC\x8E") == 0) {  // ．
      matched = true;
      marker_len = 3;
    } else if (core.compare(i, 3, "\xE3\x80\x81") == 0) {  // 、
      matched = true;
      marker_len = 3;
    } else if (core[i] == '.') {
      matched = true;
      marker_len = 1;
    }
  }
  if (!matched) {
    // 罗马数字前缀（半角或全角）。
    std::size_t j = 0;
    std::size_t rl = 0;
    char32_t cp = 0;
    if (first_cp(core) >= 0x2160 && first_cp(core) <= 0x2179) {
      while (j < core.size() && rl < kMaxRomanChars) {
        const std::size_t len = decode_cp(core, j, cp);
        if (!((cp >= 0x2160 && cp <= 0x2169) ||
              (cp >= 0x2170 && cp <= 0x2179))) {
          break;
        }
        j += len;
        ++rl;
      }
    } else {
      while (j < core.size() && is_roman_letter(core[j]) && j < kMaxRomanChars) ++j;
      rl = j;
    }
    if (rl > 0 && j < core.size()) {
      if (core.compare(j, 3, "\xEF\xBC\x8E") == 0) {
        matched = true;
        marker_len = 3;
        i = j;
      } else if (core[j] == '.') {
        matched = true;
        marker_len = 1;
        i = j;
      } else if (core.compare(j, 3, "\xE3\x80\x81") == 0) {
        matched = true;
        marker_len = 3;
        i = j;
      }
    }
  }
  if (!matched) return {};

  const std::size_t title_pos = i + marker_len;
  const std::string_view rest(core.data() + title_pos,
                              core.size() - title_pos);
  const std::string remainder = trim_ws(rest);
  if (remainder.empty()) return {};
  if (!has_letter_or_han(remainder)) return {};
  // "1.5 米" and similar continuations of a number are not headings.
  const char32_t first = first_cp(remainder);
  if (first >= U'0' && first <= U'9') return {};
  if (first >= 0xFF10 && first <= 0xFF19) return {};
  if (rest_looks_like_sentence(remainder, false)) {
    return {};
  }
  return make_match(2, core, kPlainNumber);
}

Match match_unnumbered(const std::string& core, const ParseOptions& options) {
  const std::size_t length = cp_count(core);
  if (length == 0) return {};
  if (length > static_cast<std::size_t>(std::max(1, options.max_title_chars))) {
    return {};
  }
  const char32_t first = first_cp(core);
  const char32_t last = last_cp(core);
  switch (first) {
    case U'\u300C':  // 「
    case U'\u300E':  // 『
    case U'\u201C':  // “
    case U'\u2018':  // ‘
    case U'"':
    case U'\'':
      return {};
    default:
      break;
  }
  switch (last) {
    case U'\u3002':
    case U'\uFF01':
    case U'\uFF1F':
    case U'\u2026':
    case U'!':
    case U'?':
    case U'\uFF1B':
    case U';':
    case U'.':
    case U'\uFF0E':  // ．
    case U'\u2500':  // ─
    case U'\uFF5E':  // ～
    case U'\u2014':  // —
      return {};
    case U'\u300D':  // 」
    case U'\u300F':  // 』
    case U'\u201D':  // ”
    case U'\u2019':  // ’
    case U'"':
      return {};
    default:
      break;
  }
  if (core.find("\xEF\xBC\x9A") != std::string::npos) return {};  // ：
  if (core.find("\xEF\xBC\x8C") != std::string::npos) return {};  // ，
  if (core.find("\xE3\x80\x81") != std::string::npos) return {};  // 、
  if (core.find("\xEF\xBC\x9B") != std::string::npos) return {};  // ；
  if (core.find(':') != std::string::npos) return {};
  if (core.find(',') != std::string::npos) return {};
  if (core.find(';') != std::string::npos) return {};
  return make_match(2, core, kUnnumbered);
}

// 标题候选的字节级预筛：只有首字符在此集合内的行才可能命中任何标题规则。
// 数值覆盖：'#'（Markdown）、'第'（编号）、特殊章名词首字、英文关键词首字母
// 与罗马数字字母、半角/全角数字、罗马数字字符、括号、装饰符。
bool plausible_heading_start(char32_t cp) {
  if (cp == U'#' || cp == U'第') return true;
  switch (cp) {
    case U'序':
    case U'楔':
    case U'引':
    case U'前':
    case U'尾':
    case U'终':
    case U'后':
    case U'番':
    case U'外':
    case U'传':
    case U'特':
    case U'附':
    case U'跋':
      return true;
    default:
      break;
  }
  if (cp >= U'0' && cp <= U'9') return true;
  if (cp >= 0xFF10 && cp <= 0xFF19) return true;  // 全角数字
  if (cp >= 0x2160 && cp <= 0x2179) return true;  // 罗马数字 Ⅰ-Ⅻ
  if (cp >= U'A' && cp <= U'Z') {
    switch (cp) {
      case U'C':
      case U'V':
      case U'P':
      case U'E':
      case U'B':
      case U'I':
      case U'X':
      case U'L':
      case U'D':
      case U'M':
        return true;
      default:
        return false;
    }
  }
  if (cp >= U'a' && cp <= U'z') {
    switch (cp) {
      case U'c':
      case U'v':
      case U'p':
      case U'e':
      case U'b':
      case U'i':
      case U'x':
      case U'l':
      case U'd':
      case U'm':
        return true;
      default:
        return false;
    }
  }
  switch (cp) {
    case U'(':
    case U'\uFF08':  // （
    case U'\u3010':  // 【
    case U'\u3016':  // 〖
    case U'\u3014':  // 〔
    case U'\u3008':  // 〈
    case U'\u300A':  // 《
    case U'=':
    case U'*':
    case U'\u2606':  // ☆
    case U'\u2605':  // ★
    case U'\u2014':  // —
      return true;
    default:
      return false;
  }
}

Match match_heading(std::string_view raw, const ParseOptions& options) {
  const std::string_view trimmed_view = trim_ws_view(raw);
  if (trimmed_view.empty()) return {};

  // 预筛：正文长行直接返回，不产生任何堆分配。
  // 任何标题都受 max_title_chars 限制，4 字节/码点 + 64 是安全上界。
  const std::size_t limit =
      4 * static_cast<std::size_t>(std::max(1, options.max_title_chars)) + kTitleByteSlack;
  // Markdown 是显式标题，历史上不受 max_title_chars 限制，保持原行为。
  if (trimmed_view[0] != '#' && trimmed_view.size() > limit) return {};
  // --title-lines 需要检查无编号短行，不能按首字符白名单过滤。
  if (!options.accept_unnumbered_titles &&
      !plausible_heading_start(first_cp(trimmed_view))) {
    return {};
  }

  std::string trimmed(trimmed_view);

  int forced_level = 0;
  // Markdown 井号：显式标记，不再做正文判定。
  {
    std::size_t hashes = 0;
    while (hashes < trimmed.size() && trimmed[hashes] == '#') ++hashes;
    if (hashes >= 1 && hashes <= 3 && hashes < trimmed.size()) {
      if (trimmed[hashes] == ' ' || trimmed[hashes] == '\t') {
        const std::string title = collapse_ws(trimmed.substr(hashes));
        if (!title.empty()) {
          // 单个井号是文件自身的顶层；只有标题本身写卷 / 部 / 篇才算卷，
          // 「# 第一章」仍是章。
          int level = 2;
          if (hashes == 1) {
            const Match numbered = match_numbered(title);
            if (numbered.ok && numbered.level == 1) level = 1;
          }
          return make_match(level, title, kMarkdown);
        }
      } else {
        // "###第一章 启程": drop the hashes, parse the rest as a normal title.
        const std::string rest = trim_ws(trimmed.substr(hashes));
        if (!rest.empty()) {
          trimmed = rest;
          forced_level = (hashes == 1) ? 1 : 2;
        }
      }
    }
  }

  const std::string core = collapse_ws(strip_decorations(trimmed));
  if (core.empty()) return {};
  if (!has_letter_or_han(core)) return {};
  if (cp_count(core) > static_cast<std::size_t>(
                           std::max(1, options.max_title_chars))) {
    return {};
  }
  // 句末标点之后还有内容，视为正文。
  if (has_embedded_sentence_end(core)) return {};

  Match match;
  const auto take = [&match](const Match& candidate) {
    if (!candidate.ok) return false;
    match = candidate;
    return true;
  };
  if (take(match_numbered(core))) {
  } else if (take(match_special(core))) {
  } else if (take(match_english(core))) {
  } else if (take(match_bracket(core))) {
  } else if (take(match_plain_number(core))) {
  } else if (options.accept_unnumbered_titles) {
    take(match_unnumbered(core, options));
  }
  if (!match.ok) return {};
  if (forced_level != 0) match.level = forced_level;
  return match;
}

struct Candidate {
  std::size_t line = 0;
  int level = 2;
  int kind = 0;
  std::string title;
};

bool looks_like_toc_label(std::string_view line) {
  if (line.find("\xE7\x9B\xAE\xE5\xBD\x95") != std::string_view::npos) {
    return true;  // 目录
  }
  if (line.find("\xE7\x9B\xAE\xE6\xAC\xA1") != std::string_view::npos) {
    return true;  // 目次
  }
  const std::string low = ascii_lower(line);
  return low.find("contents") != std::string::npos;
}

}  // namespace

ParseResult parse_chapters(std::string_view utf8_text,
                           const ParseOptions& options) {
  ParseResult result;
  // 容忍未归一化的 CRLF 输入；TextFile 通常已在此前处理。
  std::string normalized;
  std::string_view text = utf8_text;
  if (utf8_text.find('\r') != std::string_view::npos) {
    normalized.assign(utf8_text);
    normalize_newlines(normalized);
    text = normalized;
  }
  const std::vector<std::string_view> lines = split_lines_view(text);

  std::vector<Candidate> all;
  all.reserve(lines.size());
  for (std::size_t i = 0; i < lines.size(); ++i) {
    const Match m = match_heading(lines[i], options);
    if (m.ok) {
      Candidate c;
      c.line = i;
      c.level = m.level;
      c.kind = m.kind;
      c.title = m.title;
      all.push_back(std::move(c));
    }
  }

  std::vector<bool> toc(lines.size(), false);
  std::vector<bool> excluded(all.size(), false);

  // 开头目录区：连续不少于 3 个候选标题，中间只有空行，
  // 且带「目录」类标签或与后文标题重复。
  constexpr std::size_t kOpeningLimit = 300;
  std::size_t index = 0;
  while (index < all.size()) {
    std::size_t last = index;
    while (last + 1 < all.size()) {
      bool only_blank = true;
      for (std::size_t l = all[last].line + 1; l < all[last + 1].line; ++l) {
        if (!is_blank_line(lines[l])) {
          only_blank = false;
          break;
        }
      }
      if (!only_blank) break;
      ++last;
    }

    if (last - index + 1 >= 3 && all[index].line < kOpeningLimit) {
      std::size_t head_end = last + 1;
      {
        // run 内首次出现重复标题的位置即目录区结尾。all 在扫描期间不修改，
        // string_view 直接指向候选标题，避免在热路径拷贝标题。
        std::unordered_set<std::string_view> seen;
        seen.reserve(last - index + 1);
        for (std::size_t a = index; a <= last; ++a) {
          if (!seen.insert(std::string_view(all[a].title)).second) {
            head_end = a;
            break;
          }
        }
      }

      if (head_end - index >= 3) {
        std::vector<std::size_t> labels;
        int seen = 0;
        std::size_t probe = all[index].line;
        while (probe > 0 && seen < 2) {
          --probe;
          if (is_blank_line(lines[probe])) continue;
          ++seen;
          if (looks_like_toc_label(lines[probe])) labels.push_back(probe);
        }

        std::size_t reappear = 0;
        {
          // 标题 -> 该标题在 head_end 之后最后一次出现的候选下标。
          std::unordered_map<std::string_view, std::size_t> later;
          later.reserve(all.size() - head_end);
          for (std::size_t b = head_end; b < all.size(); ++b) {
            later[std::string_view(all[b].title)] = b;
          }
          for (std::size_t a = index; a < head_end; ++a) {
            const auto it = later.find(std::string_view(all[a].title));
            if (it != later.end() &&
                all[it->second].line > all[head_end - 1].line) {
              ++reappear;
            }
          }
        }

        if (!labels.empty() || reappear >= 2) {
          for (std::size_t a = index; a < head_end; ++a) {
            toc[all[a].line] = true;
            excluded[a] = true;
          }
          for (std::size_t label : labels) toc[label] = true;
          break;
        }
      }
    }

    index = last + 1;
  }

  // 连续无正文的纯数字 / 括号标题判为列表项或裸目录，不算章节。
  std::size_t list_lines = 0;
  {
    std::size_t a = 0;
    while (a < all.size()) {
      const bool listable =
          (all[a].kind == kPlainNumber || all[a].kind == kBracket);
      if (excluded[a] || !listable) {
        ++a;
        continue;
      }
      std::size_t b = a;
      while (b + 1 < all.size() && !excluded[b + 1] &&
             (all[b + 1].kind == kPlainNumber ||
              all[b + 1].kind == kBracket)) {
        bool only_blank = true;
        for (std::size_t l = all[b].line + 1; l < all[b + 1].line; ++l) {
          if (!is_blank_line(lines[l])) {
            only_blank = false;
            break;
          }
        }
        if (!only_blank) break;
        ++b;
      }
      if (b > a) {
        for (std::size_t k = a; k <= b; ++k) excluded[k] = true;
        list_lines += b - a + 1;
      }
      a = b + 1;
    }
  }
  if (list_lines > 0) {
    result.warnings.push_back("已忽略 " + std::to_string(list_lines) +
                              " 行连续编号（判为列表/目录，不当作章节）");
  }

  std::vector<Candidate> body;
  body.reserve(all.size());
  for (std::size_t a = 0; a < all.size(); ++a) {
    if (!excluded[a]) body.push_back(std::move(all[a]));
  }

  for (std::size_t k = 0; k < body.size(); ++k) {
    Chapter chapter;
    chapter.level = body[k].level;
    chapter.title = body[k].title;
    chapter.heading_line = std::string(lines[body[k].line]);
    chapter.start_line = body[k].line;
    chapter.end_line = (k + 1 < body.size()) ? body[k + 1].line : lines.size();
    chapter.synthetic = false;
    split_heading_display(chapter.title, chapter);
    result.chapters.push_back(std::move(chapter));
  }

  if (!result.chapters.empty()) {
    const std::size_t first_line = result.chapters.front().start_line;
    bool has_body = false;
    for (std::size_t l = 0; l < first_line; ++l) {
      if (toc[l]) continue;
      if (!is_blank_line(lines[l])) {
        has_body = true;
        break;
      }
    }
    if (has_body) {
      Chapter preface;
      preface.level = 2;
      preface.title = "（前言）";
      preface.start_line = 0;
      preface.end_line = first_line;
      preface.synthetic = true;
      split_heading_display(preface.title, preface);
      result.chapters.insert(result.chapters.begin(), std::move(preface));
    }
  } else {
    result.warnings.push_back("未识别到章节标题");
    Chapter whole;
    whole.level = 2;
    whole.title = "（全文）";
    whole.start_line = 0;
    whole.end_line = lines.size();
    whole.synthetic = true;
    split_heading_display(whole.title, whole);
    result.chapters.push_back(std::move(whole));
  }

  for (std::size_t i = 0; i < result.chapters.size(); ++i) {
    result.chapters[i].index = static_cast<int>(i);
  }
  for (std::size_t i = 0; i < toc.size(); ++i) {
    if (toc[i]) result.toc_lines.push_back(i);
  }
  return result;
}

namespace {

// 把「单位之后/数字之后」的原文切成 separator + name。
void assign_columns(Chapter& chapter, const std::string& number,
                    std::string_view rest, bool numbered) {
  chapter.number = number;
  const std::size_t sep = leading_sep_len(rest);
  chapter.separator = std::string(rest.substr(0, sep));
  chapter.name = trim_ws(rest.substr(sep));
  chapter.numbered = numbered;
}

// 纯数字/罗马数字编号后的标点长度（"1." / "1、" / "1．"）。
std::size_t plain_marker_len(std::string_view s, std::size_t pos) {
  if (pos >= s.size()) return 0;
  if (s.compare(pos, 3, "\xEF\xBC\x8E") == 0) return 3;  // ．
  if (s.compare(pos, 3, "\xE3\x80\x81") == 0) return 3;  // 、
  if (s[pos] == '.') return 1;
  return 0;
}

}  // namespace

void split_heading_display(const std::string& full_title, Chapter& chapter) {
  chapter.number.clear();
  chapter.separator.clear();
  chapter.name = full_title;
  chapter.numbered = false;
  if (full_title.empty()) return;
  const std::string_view title(full_title);

  // 1) 第 <数字> <单位>：可递增序号，number 到单位为止。
  const ChineseNumberedScan scan = scan_chinese_numbered(title);
  if (scan.ok) {
    assign_columns(chapter, std::string(title.substr(0, scan.unit_end)),
                   title.substr(scan.unit_end), true);
    return;
  }

  // 2) 特殊章名，可带数字；序号列原样显示但不参与自动编号。
  for (const std::string_view word : kSpecialWords) {
    if (title.compare(0, word.size(), word) != 0) continue;
    std::size_t i = word.size();
    while (i < title.size()) {
      char32_t cp = 0;
      const std::size_t len = decode_cp(title, i, cp);
      if (!is_number_cp(cp)) break;
      i += len;
    }
    assign_columns(chapter, std::string(title.substr(0, i)), title.substr(i),
                   false);
    return;
  }

  // 3) 英文关键词 + 编号。
  const std::string low = ascii_lower(title);
  for (const std::string_view word : kEnglishWords) {
    const std::size_t len = word.size();
    if (low.size() < len || low.compare(0, len, word) != 0) continue;
    if (low.size() > len &&
        std::isalpha(static_cast<unsigned char>(low[len]))) {
      continue;
    }
    std::size_t i = len;
    i += leading_sep_len(title.substr(i));
    const std::size_t number_len = consume_number_token(low, i);
    if (number_len == 0) {
      // Prologue / Epilogue 等无数字关键词：整词作序号列，不参与递增。
      assign_columns(chapter, std::string(title.substr(0, len)),
                     title.substr(len), false);
      return;
    }
    i += number_len;
    assign_columns(chapter, std::string(title.substr(0, i)), title.substr(i),
                   true);
    return;
  }

  // 4) 括号编号 (1) / （一）。
  {
    const char32_t open = first_cp(title);
    if (open == U'(' || open == U'\uFF08') {
      const std::size_t open_len = (open == U'(') ? 1 : 3;
      const std::string close = (open == U'(') ? ")" : "\xEF\xBC\x89";
      const std::size_t close_pos = title.find(close, open_len);
      if (close_pos != std::string_view::npos) {
        const std::string_view inside =
            title.substr(open_len, close_pos - open_len);
        bool all_numbers = !inside.empty();
        for (std::size_t k = 0; k < inside.size();) {
          char32_t cp = 0;
          const std::size_t len = decode_cp(inside, k, cp);
          if (!is_number_cp(cp)) {
            all_numbers = false;
            break;
          }
          k += len;
        }
        if (all_numbers) {
          const std::size_t end = close_pos + close.size();
          assign_columns(chapter, std::string(title.substr(0, end)),
                         title.substr(end), true);
          return;
        }
      }
    }
  }

  // 5) 纯数字 "1." / "1、" / "1．"。
  {
    std::size_t i = 0;
    std::size_t digits = 0;
    while (i < title.size() && title[i] >= '0' && title[i] <= '9' &&
           digits < kMaxNumberDigits) {
      ++i;
      ++digits;
    }
    if (digits >= 1 && digits <= 4) {
      const std::size_t marker = plain_marker_len(title, i);
      if (marker > 0) {
        const std::size_t end = i + marker;
        assign_columns(chapter, std::string(title.substr(0, end)),
                       title.substr(end), true);
        return;
      }
    }
  }

  // 6) 罗马数字 "I." / "Ⅳ．"。
  {
    std::size_t j = 0;
    char32_t cp = 0;
    if (first_cp(title) >= 0x2160 && first_cp(title) <= 0x2179) {
      while (j < title.size() && j < kMaxRomanChars * 3) {
        const std::size_t len = decode_cp(title, j, cp);
        if (!((cp >= 0x2160 && cp <= 0x2169) ||
              (cp >= 0x2170 && cp <= 0x2179))) {
          break;
        }
        j += len;
      }
    } else {
      while (j < title.size() && is_roman_letter(title[j]) && j < kMaxRomanChars) ++j;
    }
    if (j > 0) {
      const std::size_t marker = plain_marker_len(title, j);
      if (marker > 0) {
        const std::size_t end = j + marker;
        assign_columns(chapter, std::string(title.substr(0, end)),
                       title.substr(end), true);
        return;
      }
    }
  }

  // 7) 无前缀：整条标题就是 name。
}

}  // namespace novel
