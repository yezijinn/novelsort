#include "core/Script.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/Json.hpp"
#include "core/TextUtf8.hpp"

namespace novel {
namespace {

constexpr char32_t kAsciiQuote = static_cast<char32_t>('"');

struct LineSpan {
  std::size_t begin = 0;
  std::size_t end = 0;  // 不含行尾 '\r'
};

// 按 '\n' 切行并记录每行的字节区间；行尾 '\r' 不计入内容。
std::vector<LineSpan> split_byte_lines(std::string_view text) {
  std::vector<LineSpan> lines;
  std::size_t start = 0;
  while (true) {
    const std::size_t newline = text.find('\n', start);
    std::size_t end = newline == std::string_view::npos ? text.size() : newline;
    if (end > start && text[end - 1] == '\r') --end;
    lines.push_back(LineSpan{start, end});
    if (newline == std::string_view::npos) break;
    start = newline + 1;
  }
  return lines;
}

bool is_han(char32_t cp) {
  return (cp >= 0x3400 && cp <= 0x4DBF) || (cp >= 0x4E00 && cp <= 0x9FFF) ||
         (cp >= 0xF900 && cp <= 0xFAFF);
}

struct QuotePair {
  char32_t open;
  char32_t close;
};

constexpr QuotePair kQuotePairs[] = {
    {U'\u201C', U'\u201D'},  // “”
    {U'\u300C', U'\u300D'},  // 「」
    {U'\u300E', U'\u300F'},  // 『』
    {U'\u2018', U'\u2019'},  // ‘’
    {kAsciiQuote, kAsciiQuote},
};

// 该码点是否为开引号；是则给出对应闭引号。
bool quote_close_for(char32_t cp, char32_t& close) {
  for (const QuotePair& pair : kQuotePairs) {
    if (pair.open == cp) {
      close = pair.close;
      return true;
    }
  }
  return false;
}

// 取 pos 之前那个码点的起始字节。
std::size_t previous_cp(std::string_view text, std::size_t pos) {
  if (pos == 0) return 0;
  std::size_t start = pos - 1;
  while (start > 0 &&
         (static_cast<unsigned char>(text[start]) & 0xC0) == 0x80) {
    --start;
  }
  return start;
}

// trim、换行与连续空白压成一个空格；不做其它改写。
std::string normalize_speak(std::string_view text) {
  const std::string_view trimmed = trim_ws_view(text);
  std::string out;
  out.reserve(trimmed.size());
  bool pending_space = false;
  std::size_t pos = 0;
  while (pos < trimmed.size()) {
    char32_t cp = 0;
    const std::size_t len = decode_cp(trimmed, pos, cp);
    if (is_space_cp(cp)) {
      pending_space = !out.empty();
    } else {
      if (pending_space) out.push_back(' ');
      pending_space = false;
      append_utf8(out, cp);
    }
    pos += len;
  }
  return out;
}

constexpr std::string_view kThoughtMarkers[] = {
    "心想", "暗道", "心中想", "暗想", "寻思",
    "心里想", "脑海里", "内心",
};

constexpr std::string_view kSpeechVerbs[] = {
    "冷笑道", "轻声道", "沉声道", "低声道", "高声道", "接口道", "说道",
    "问道",   "答道",   "叫道",   "吼道",   "嚷道",   "嘟囔",   "呢喃",
    "惊呼",   "叹息",   "笑道",   "说",     "道",     "问",     "答",
    "喊",     "叫",
};

// 归属语后缀，按长度降序尝试剥离。
constexpr std::string_view kHintSuffixes[] = {
    "冷冷地", "淡淡地", "轻轻地", "急忙", "连忙", "轻声", "低声", "高声",
    "沉声",   "急声",   "柔声",   "微笑", "冷笑", "轻笑", "苦笑", "点头",
    "摇头",   "叹息",   "笑着",   "哭着",
};

bool has_thought_marker(std::string_view src, std::size_t open,
                        std::size_t lower) {
  const std::size_t window_begin =
      open > lower && open - lower > 40 ? open - 40 : lower;
  if (window_begin >= open) return false;
  const std::string_view window = src.substr(window_begin, open - window_begin);
  for (const std::string_view marker : kThoughtMarkers) {
    if (window.find(marker) != std::string_view::npos) return true;
  }
  return false;
}

struct VerbHit {
  std::size_t pos = std::string_view::npos;
  std::size_t length = 0;
};

// 窗口内结束位置最靠后的动词（最靠近引号）。
VerbHit nearest_before(std::string_view window) {
  VerbHit best;
  std::size_t best_end = 0;
  for (const std::string_view verb : kSpeechVerbs) {
    std::size_t pos = window.find(verb);
    while (pos != std::string_view::npos) {
      const std::size_t end = pos + verb.size();
      if (end > best_end || (end == best_end && verb.size() > best.length)) {
        best.pos = pos;
        best.length = verb.size();
        best_end = end;
      }
      pos = window.find(verb, pos + 1);
    }
  }
  return best;
}

// 窗口内起始位置最靠前的动词（最靠近引号）。
VerbHit nearest_after(std::string_view window) {
  VerbHit best;
  for (const std::string_view verb : kSpeechVerbs) {
    std::size_t pos = window.find(verb);
    while (pos != std::string_view::npos) {
      if (best.pos == std::string_view::npos || pos < best.pos ||
          (pos == best.pos && verb.size() > best.length)) {
        best.pos = pos;
        best.length = verb.size();
      }
      pos = window.find(verb, pos + 1);
    }
  }
  return best;
}

// 从动词起点往前剥离修饰语与常见后缀，返回剥离后的终点。
// 修饰语可能叠加（如「微笑着」= 后缀 + 助词），取消耗字符最多的解析。
struct TailTrim {
  std::size_t cursor = 0;
  std::size_t consumed = 0;
};

TailTrim trim_hint_tail(std::string_view src, std::size_t cursor,
                        std::size_t lower, int depth) {
  TailTrim best{cursor, 0};
  if (depth <= 0 || cursor <= lower) return best;
  for (const std::string_view suffix : kHintSuffixes) {
    if (cursor < lower + suffix.size()) continue;
    if (src.substr(cursor - suffix.size(), suffix.size()) != suffix) continue;
    const TailTrim sub =
        trim_hint_tail(src, cursor - suffix.size(), lower, depth - 1);
    const std::size_t total = cp_count(suffix) + sub.consumed;
    if (total > best.consumed) best = TailTrim{sub.cursor, total};
  }
  const std::size_t start = previous_cp(src, cursor);
  if (start >= lower && start < cursor) {
    char32_t cp = 0;
    decode_cp(src, start, cp);
    if (cp == U'地' || cp == U'着' || cp == U'的') {
      const TailTrim sub = trim_hint_tail(src, start, lower, depth - 1);
      if (1 + sub.consumed > best.consumed) {
        best = TailTrim{sub.cursor, 1 + sub.consumed};
      }
    }
  }
  return best;
}

// 从 cursor 往前收集最多 3 个连续汉字。
std::string collect_hint(std::string_view src, std::size_t cursor,
                         std::size_t lower) {
  std::vector<char32_t> chars;
  chars.reserve(3);
  std::size_t pos = cursor;
  while (pos > lower && chars.size() < 3) {
    const std::size_t start = previous_cp(src, pos);
    if (start < lower) break;
    char32_t cp = 0;
    decode_cp(src, start, cp);
    if (!is_han(cp)) break;
    chars.push_back(cp);
    pos = start;
  }
  std::string out;
  out.reserve(chars.size() * 3);
  for (auto it = chars.rbegin(); it != chars.rend(); ++it) {
    append_utf8(out, *it);
  }
  return out;
}

constexpr std::size_t kHintWindow = 24;

// 常见非人名的两三字短语，避免把语气/动作词当成说话人。
constexpr std::string_view kHintStopwords[] = {
    "小声", "低声", "高声", "轻声", "沉声", "闷声", "出声", "声音",
    "温柔", "冷冷", "淡淡", "轻轻", "急忙", "连忙", "笑着", "哭着",
    "这是", "那是", "自己", "对方", "众人", "大家", "有人", "旁人",
    "语气", "神色", "表情", "然后", "接着", "继续", "突然", "忽然",
};

bool is_hint_stopword(std::string_view hint) {
  for (const std::string_view word : kHintStopwords) {
    if (hint == word) return true;
  }
  return false;
}

// 常见姓氏。只保留以姓氏开头的 2–3 字候选，优先保证 hint 不乱猜。
constexpr std::u32string_view kSurnames =
    U"赵钱孙李周吴郑王冯陈褚卫蒋沈韩杨朱秦尤许何吕施张孔曹严华金魏陶姜"
    U"戚谢邹喻柏水窦章云苏潘葛奚范彭郎鲁韦昌马苗凤花方俞任袁柳鲍史唐"
    U"费廉岑薛雷贺倪汤滕殷罗毕郝邬安常乐于时傅皮卞齐康伍余元卜顾孟平"
    U"黄和穆萧尹姚邵湛汪祁毛禹狄米贝明臧计伏成戴谈宋茅庞熊纪舒屈项祝"
    U"董梁杜阮蓝闵席季麻强贾路娄危江童颜郭梅盛林刁钟徐邱骆高夏蔡田樊"
    U"胡凌霍虞万支柯昝管卢莫经房裘缪干解应宗丁宣邓郁单杭洪包诸左石崔"
    U"吉钮龚程嵇邢滑裴陆荣翁荀羊惠甄曲家封芮羿储靳汲邴糜松井段富巫乌"
    U"焦巴弓牧隗山谷车侯宓蓬全郗班仰秋仲伊宫宁仇栾暴甘厉戎祖武符刘景"
    U"詹束龙叶幸司韶郜黎蓟薄印宿白怀蒲邰从鄂索咸籍赖卓蔺屠蒙池乔阴胥"
    U"能苍双闻莘党翟谭贡劳逄姬申扶堵冉宰郦雍桑桂濮牛寿通边扈燕冀浦尚"
    U"农温别庄晏柴瞿阎充慕连茹习宦艾鱼容向古易慎戈廖庾终暨居衡步都耿"
    U"满弘匡国文寇广禄阙东欧沃利蔚越隆师巩聂晁勾敖融冷辛阚简饶曾沙养"
    U"鞠须丰巢关蒯相查后荆红游竺权盖益桓公";

bool is_pronoun(std::string_view hint) {
  return hint == "他" || hint == "她" || hint == "它" || hint == "我" ||
         hint == "你" || hint == "您" || hint == "他们" || hint == "她们" ||
         hint == "我们" || hint == "你们";
}

std::string accept_hint(std::string hint) {
  if (hint.empty() || is_hint_stopword(hint)) return {};
  const std::size_t length = cp_count(hint);
  if (length == 1) return is_pronoun(hint) ? hint : std::string();
  if (length == 2 || length == 3) {
    return kSurnames.find(first_cp(hint)) == std::u32string_view::npos
               ? std::string()
               : hint;
  }
  return {};
}

std::string find_speaker_hint(std::string_view src, std::size_t open,
                              std::size_t close_end, std::size_t lower,
                              std::size_t upper) {
  if (open > lower) {
    const std::size_t window_begin =
        open - lower > kHintWindow ? open - kHintWindow : lower;
    const std::string_view window = src.substr(window_begin, open - window_begin);
    const VerbHit hit = nearest_before(window);
    if (hit.pos != std::string_view::npos) {
      const std::size_t verb_at = window_begin + hit.pos;
      const TailTrim trim = trim_hint_tail(src, verb_at, lower, 4);
      std::string hint = accept_hint(collect_hint(src, trim.cursor, lower));
      if (!hint.empty()) return hint;
    }
  }
  if (close_end < upper) {
    const std::size_t window_end =
        upper - close_end > kHintWindow ? close_end + kHintWindow : upper;
    const std::string_view window = src.substr(close_end, window_end - close_end);
    const VerbHit hit = nearest_after(window);
    if (hit.pos != std::string_view::npos) {
      const std::size_t verb_at = close_end + hit.pos;
      const TailTrim trim = trim_hint_tail(src, verb_at, lower, 4);
      std::string hint = accept_hint(collect_hint(src, trim.cursor, lower));
      if (!hint.empty()) return hint;
    }
  }
  return {};
}

// 对话 speak_text：去掉最外层引号后压缩空白；未闭合时只去掉开引号。
std::string quote_speak_text(std::string_view text, bool closed) {
  std::string_view body = text;
  if (body.empty()) return {};
  char32_t first = 0;
  const std::size_t first_len = decode_cp(body, 0, first);
  char32_t close = 0;
  if (quote_close_for(first, close)) {
    body = body.substr(first_len);
    if (closed && !body.empty()) {
      const std::size_t last_start = previous_cp(body, body.size());
      char32_t last = 0;
      decode_cp(body, last_start, last);
      if (last == close) body = body.substr(0, last_start);
    }
  }
  return normalize_speak(body);
}

}  // namespace

const char* script_type_name(ScriptSegmentType type) {
  switch (type) {
    case ScriptSegmentType::heading:
      return "heading";
    case ScriptSegmentType::narration:
      return "narration";
    case ScriptSegmentType::dialogue:
      return "dialogue";
    case ScriptSegmentType::thought:
      return "thought";
  }
  return "unknown";
}

ScriptDocument build_script(std::string_view text,
                            const std::vector<Chapter>& chapters,
                            const std::vector<std::size_t>& toc_lines,
                            std::string source, std::string encoding,
                            const ScriptBuildOptions& options) {
  ScriptDocument document;
  document.source = std::move(source);
  document.encoding = std::move(encoding);
  document.characters.push_back(
      ScriptCharacter{"narrator", "旁白", "", true, {}});

  const std::vector<LineSpan> lines = split_byte_lines(text);
  const std::size_t line_count = lines.size();

  std::vector<bool> in_toc(line_count, false);
  for (const std::size_t line : toc_lines) {
    if (line < line_count) in_toc[line] = true;
  }

  const auto line_begin = [&](std::size_t line) -> std::size_t {
    return line < line_count ? lines[line].begin : text.size();
  };

  int next_id = 1;

  for (std::size_t chapter_pos = 0; chapter_pos < chapters.size();
       ++chapter_pos) {
    const Chapter& chapter = chapters[chapter_pos];
    const int chapter_index = static_cast<int>(chapter_pos) + 1;
    // inserted 章的标题不在原文，按合成章处理：没有标题片段。
    const bool synthetic = chapter.synthetic || chapter.inserted;
    const std::size_t start_line = std::min(chapter.start_line, line_count);
    const std::size_t end_line = std::min(chapter.end_line, line_count);
    const std::size_t body_begin_offset = line_begin(start_line);
    const std::size_t body_end_offset = line_begin(end_line);

    ScriptChapter script_chapter;
    script_chapter.index = chapter_index;
    script_chapter.title = chapter.title;
    script_chapter.level = chapter.level;
    script_chapter.synthetic = synthetic;
    script_chapter.start_line = chapter.start_line;
    script_chapter.end_line = chapter.end_line;
    script_chapter.source_start = synthetic ? 0 : body_begin_offset;
    script_chapter.source_end = synthetic ? 0 : body_end_offset;
    document.chapters.push_back(std::move(script_chapter));

    const auto emit_plain = [&](ScriptSegmentType type, std::size_t begin,
                                std::size_t end,
                                const std::string* speak_override) {
      if (begin >= end) return;
      const std::string_view raw = text.substr(begin, end - begin);
      const std::string_view trimmed = trim_ws_view(raw);
      if (trimmed.empty()) return;
      const std::size_t trimmed_begin =
          begin + static_cast<std::size_t>(trimmed.data() - raw.data());

      ScriptSegment segment;
      segment.id = next_id++;
      segment.chapter_index = chapter_index;
      segment.type = type;
      segment.speaker_id = "narrator";
      segment.text = std::string(trimmed);
      segment.speak_text =
          speak_override == nullptr ? normalize_speak(trimmed)
                                    : normalize_speak(*speak_override);
      segment.source_start = trimmed_begin;
      segment.source_end = trimmed_begin + trimmed.size();
      segment.confidence = 1.0f;
      document.segments.push_back(std::move(segment));
    };

    const auto emit_quote = [&](std::size_t begin, std::size_t end,
                                bool closed) {
      if (begin >= end) return;
      const std::string_view raw = text.substr(begin, end - begin);
      const std::string_view trimmed = trim_ws_view(raw);
      if (trimmed.empty()) return;
      const std::size_t trimmed_begin =
          begin + static_cast<std::size_t>(trimmed.data() - raw.data());
      const std::size_t trimmed_end = trimmed_begin + trimmed.size();

      ScriptSegment segment;
      segment.id = next_id++;
      segment.chapter_index = chapter_index;
      const bool thought =
          closed && has_thought_marker(text, trimmed_begin, body_begin_offset);
      segment.type =
          thought ? ScriptSegmentType::thought : ScriptSegmentType::dialogue;
      if (closed) {
        segment.speaker_hint = find_speaker_hint(
            text, trimmed_begin, trimmed_end, body_begin_offset,
            body_end_offset);
      }
      segment.text = std::string(trimmed);
      segment.speak_text = quote_speak_text(trimmed, closed);
      segment.source_start = trimmed_begin;
      segment.source_end = trimmed_end;
      segment.confidence = closed ? 0.9f : 0.3f;
      segment.needs_review = !closed;
      document.segments.push_back(std::move(segment));
    };

    if (!synthetic && options.include_headings && start_line < end_line &&
        start_line < line_count && !in_toc[start_line]) {
      emit_plain(ScriptSegmentType::heading, lines[start_line].begin,
                 lines[start_line].end, &chapter.title);
    }

    const std::size_t body_start = synthetic ? start_line : start_line + 1;

    struct QuoteState {
      bool open = false;
      std::size_t start = 0;
      std::vector<char32_t> stack;
    } state;

    for (std::size_t line = body_start; line < end_line; ++line) {
      if (line >= line_count) break;
      // 非合成章的正文里即使出现目录样式文本，也属于正文，不能按 toc 跳过。
      if (in_toc[line] && synthetic) continue;
      const std::size_t line_begin_pos = lines[line].begin;
      const std::size_t line_end_pos = lines[line].end;
      if (is_blank_line(text.substr(line_begin_pos,
                                    line_end_pos - line_begin_pos))) {
        continue;
      }

      std::size_t pos = line_begin_pos;
      std::size_t narration_begin = line_begin_pos;
      while (pos < line_end_pos) {
        char32_t cp = 0;
        const std::size_t len = decode_cp(text, pos, cp);
        if (!state.open) {
          char32_t close = 0;
          if (quote_close_for(cp, close)) {
            emit_plain(ScriptSegmentType::narration, narration_begin, pos,
                       nullptr);
            state.open = true;
            state.start = pos;
            state.stack.clear();
            state.stack.push_back(close);
          }
          pos += len;
          continue;
        }
        if (cp == kAsciiQuote) {
          if (!state.stack.empty() && state.stack.back() == kAsciiQuote) {
            state.stack.pop_back();
            pos += len;
            if (state.stack.empty()) {
              emit_quote(state.start, pos, true);
              state.open = false;
              narration_begin = pos;
            }
          } else {
            state.stack.push_back(kAsciiQuote);
            pos += len;
          }
          continue;
        }
        char32_t close = 0;
        if (quote_close_for(cp, close)) {
          state.stack.push_back(close);
          pos += len;
          continue;
        }
        if (!state.stack.empty() && cp == state.stack.back()) {
          state.stack.pop_back();
          pos += len;
          if (state.stack.empty()) {
            emit_quote(state.start, pos, true);
            state.open = false;
            narration_begin = pos;
          }
          continue;
        }
        pos += len;
      }
      if (!state.open) {
        emit_plain(ScriptSegmentType::narration, narration_begin,
                   line_end_pos, nullptr);
      }
    }

    if (state.open) {
      const std::size_t end =
          body_end_offset > state.start ? body_end_offset : state.start;
      emit_quote(state.start, end, false);
      state.open = false;
      state.stack.clear();
    }
  }

  return document;
}

std::string script_json(const ScriptDocument& document) {
  std::string out;
  out.reserve(document.source.size() + document.encoding.size() +
              document.segments.size() * 160 + 512);

  const auto append_nullable = [&](std::string_view value) {
    if (value.empty()) {
      out += "null";
    } else {
      append_json_string(out, value);
    }
  };

  out += "{\"schema\":\"novelsort/script/1\",\"offset_unit\":\"utf8_byte\"";
  out += ",\"source\":";
  append_json_string(out, document.source);
  out += ",\"encoding\":";
  append_json_string(out, document.encoding);

  out += ",\"characters\":[";
  for (std::size_t i = 0; i < document.characters.size(); ++i) {
    if (i != 0) out.push_back(',');
    const ScriptCharacter& character = document.characters[i];
    out += "{\"id\":";
    append_json_string(out, character.id);
    out += ",\"name\":";
    append_json_string(out, character.name);
    out += ",\"gender\":";
    append_json_string(out, character.gender);
    out += ",\"is_narrator\":";
    out += character.is_narrator ? "true" : "false";
    out += ",\"aliases\":[";
    for (std::size_t k = 0; k < character.aliases.size(); ++k) {
      if (k != 0) out.push_back(',');
      append_json_string(out, character.aliases[k]);
    }
    out += "]}";
  }

  out += "],\"chapters\":[";
  for (std::size_t i = 0; i < document.chapters.size(); ++i) {
    if (i != 0) out.push_back(',');
    const ScriptChapter& chapter = document.chapters[i];
    out += "{\"index\":";
    append_json_number(out, chapter.index);
    out += ",\"title\":";
    append_json_string(out, chapter.title);
    out += ",\"level\":";
    append_json_number(out, chapter.level);
    out += ",\"synthetic\":";
    out += chapter.synthetic ? "true" : "false";
    out += ",\"start_line\":";
    append_json_number(out, static_cast<long long>(chapter.start_line));
    out += ",\"end_line\":";
    append_json_number(out, static_cast<long long>(chapter.end_line));
    out += ",\"source_start\":";
    append_json_number(out, static_cast<long long>(chapter.source_start));
    out += ",\"source_end\":";
    append_json_number(out, static_cast<long long>(chapter.source_end));
    out.push_back('}');
  }

  out += "],\"segments\":[";
  for (std::size_t i = 0; i < document.segments.size(); ++i) {
    if (i != 0) out.push_back(',');
    const ScriptSegment& segment = document.segments[i];
    out += "{\"id\":";
    append_json_number(out, segment.id);
    out += ",\"chapter\":";
    append_json_number(out, segment.chapter_index);
    out += ",\"type\":";
    append_json_string(out, script_type_name(segment.type));
    out += ",\"speaker_id\":";
    append_nullable(segment.speaker_id);
    out += ",\"speaker_hint\":";
    append_nullable(segment.speaker_hint);
    out += ",\"text\":";
    append_json_string(out, segment.text);
    out += ",\"speak_text\":";
    append_json_string(out, segment.speak_text);
    out += ",\"source_start\":";
    append_json_number(out, static_cast<long long>(segment.source_start));
    out += ",\"source_end\":";
    append_json_number(out, static_cast<long long>(segment.source_end));
    out += ",\"emotion\":";
    append_nullable(segment.emotion);
    out += ",\"tone\":";
    append_nullable(segment.tone);
    out += ",\"confidence\":";
    append_json_fixed(out, static_cast<double>(segment.confidence), 2);
    out += ",\"needs_review\":";
    out += segment.needs_review ? "true" : "false";
    out.push_back('}');
  }
  out += "]}";
  return out;
}

std::vector<std::string> validate_script(const ScriptDocument& document,
                                         std::string_view source_text) {
  std::vector<std::string> errors;
  const std::size_t chapter_count = document.chapters.size();

  for (std::size_t i = 0; i < chapter_count; ++i) {
    if (document.chapters[i].index != static_cast<int>(i) + 1) {
      errors.push_back("chapter " + std::to_string(i + 1) +
                       ": index 不连续（" +
                       std::to_string(document.chapters[i].index) + "）");
    }
  }

  int expected_id = 1;
  std::vector<std::size_t> last_end(chapter_count, 0);
  std::vector<bool> seen(chapter_count, false);

  for (const ScriptSegment& segment : document.segments) {
    const std::string label = "segment " + std::to_string(segment.id);
    if (segment.id != expected_id) {
      errors.push_back(label + ": id 不连续（期望 " +
                       std::to_string(expected_id) + "）");
    }
    ++expected_id;

    if (segment.chapter_index < 1 ||
        segment.chapter_index > static_cast<int>(chapter_count)) {
      errors.push_back(label + ": chapter 越界（" +
                       std::to_string(segment.chapter_index) + "）");
      continue;
    }
    const std::size_t chapter_slot =
        static_cast<std::size_t>(segment.chapter_index) - 1;

    if (segment.source_start >= segment.source_end) {
      errors.push_back(label + ": 偏移区间为空或反向");
      continue;
    }
    if (segment.source_end > source_text.size()) {
      errors.push_back(label + ": source_end 超出文本长度");
      continue;
    }
    const std::string_view actual =
        source_text.substr(segment.source_start,
                           segment.source_end - segment.source_start);
    if (actual != segment.text) {
      errors.push_back(label + ": text 与源文本不一致");
    }

    if (seen[chapter_slot]) {
      const std::size_t previous_end = last_end[chapter_slot];
      if (segment.source_start < previous_end) {
        errors.push_back(label + ": 与前一区间重叠");
      } else if (segment.source_start > previous_end) {
        // 合成章允许跳过 toc 行，间隙不参与覆盖检查。
        const bool synthetic_chapter =
            document.chapters[chapter_slot].synthetic;
        if (!synthetic_chapter) {
          const std::string_view gap =
              source_text.substr(previous_end, segment.source_start - previous_end);
          if (!trim_ws_view(gap).empty()) {
            errors.push_back(label + ": 与前一区间之间含非空白内容");
          }
        }
      }
    }
    seen[chapter_slot] = true;
    last_end[chapter_slot] = segment.source_end;
  }

  return errors;
}

}  // namespace novel
