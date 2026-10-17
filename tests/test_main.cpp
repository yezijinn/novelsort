#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <type_traits>
#include <vector>

#include "core/Chapter.hpp"
#include "core/ChapterOrder.hpp"
#include "core/ChapterParser.hpp"
#include "core/DirectoryEdit.hpp"
#include "core/TextFile.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

int g_checks = 0;
int g_failures = 0;
const char* g_test = "";

template <typename T>
std::string show(const T& value) {
  if constexpr (std::is_same_v<T, std::string>) {
    return value;
  } else if constexpr (std::is_arithmetic_v<T>) {
    return std::to_string(value);
  } else {
    return "<value>";
  }
}

void report_failure(const char* expression, int line, const std::string& extra) {
  ++g_failures;
  std::printf("  FAIL %s:%d  %s%s\n", g_test, line, expression,
              extra.empty() ? "" : ("  [" + extra + "]").c_str());
}

#define CHECK(cond)                                     \
  do {                                                  \
    ++g_checks;                                         \
    if (!(cond)) report_failure(#cond, __LINE__, "");    \
  } while (false)

#define CHECK_EQ(actual, expected)                                       \
  do {                                                                   \
    ++g_checks;                                                          \
    const auto& _a = (actual);                                           \
    const auto& _e = (expected);                                         \
    if (!(_a == _e)) {                                                   \
      report_failure(#actual " == " #expected, __LINE__,                 \
                     show(_a) + " != " + show(_e));                      \
    }                                                                    \
  } while (false)

std::filesystem::path u8path(const std::string& text) {
  return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::filesystem::path data_path(const std::string& relative) {
  return u8path(std::string(NOVEL_TEST_DATA) + "/" + relative);
}

novel::TextLoadResult load(const std::string& relative) {
  return novel::load_text_file(data_path(relative));
}

std::vector<std::string> titles_of(const novel::ParseResult& result) {
  std::vector<std::string> titles;
  titles.reserve(result.chapters.size());
  for (const auto& chapter : result.chapters) {
    titles.push_back(chapter.title);
  }
  return titles;
}

bool contains_title(const novel::ParseResult& result,
                    const std::string& needle) {
  for (const auto& chapter : result.chapters) {
    if (chapter.title.find(needle) != std::string::npos) return true;
  }
  return false;
}

std::vector<std::string> non_empty_lines(const std::string& text) {
  std::vector<std::string> out;
  for (const auto& line : novel::split_lines(text)) {
    if (!novel::is_blank_line(line)) out.push_back(line);
  }
  return out;
}

novel::ParseResult parse_file(const std::string& relative,
                             const novel::ParseOptions& options = {}) {
  const auto loaded = load(relative);
  if (!loaded.ok) {
    std::printf("  load failed: %s (%s)\n", relative.c_str(),
                loaded.error.c_str());
    return {};
  }
  return novel::parse_chapters(loaded.text, options);
}

#ifdef _WIN32
std::string utf8_to_gb18030(const std::string& utf8) {
  if (utf8.empty()) return {};
  const int wide_len = MultiByteToWideChar(
      CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  if (wide_len <= 0) return {};
  std::wstring wide(static_cast<std::size_t>(wide_len), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                      wide.data(), wide_len);
  const int len = WideCharToMultiByte(54936, 0, wide.data(), wide_len, nullptr,
                                      0, nullptr, nullptr);
  if (len <= 0) return {};
  std::string out(static_cast<std::size_t>(len), '\0');
  WideCharToMultiByte(54936, 0, wide.data(), wide_len, out.data(), len, nullptr,
                      nullptr);
  return out;
}
#endif

void test_standard_novel() {
  g_test = "standard_novel";
  const auto loaded = load("stage2/09_综合与回归/09-01_完整小说_标准结构.txt");
  CHECK(loaded.ok);
  const auto parsed = novel::parse_chapters(loaded.text);
  const std::vector<std::string> expected = {"序章",   "第一章 启程",
                                             "第二章 相遇", "第三章 离别",
                                             "终章",   "后记"};
  CHECK_EQ(titles_of(parsed), expected);
  CHECK(!contains_title(parsed, "夜色渐渐深了"));
  CHECK(parsed.toc_lines.empty());
}

void test_special_preface() {
  g_test = "special_preface";
  const auto parsed = parse_file("stage2/03_特殊章节名/03-01_序章_单独.txt");
  CHECK(!parsed.chapters.empty());
  if (!parsed.chapters.empty()) {
    CHECK_EQ(parsed.chapters.front().title, std::string("序章"));
    CHECK_EQ(parsed.chapters.front().level, 2);
  }
}

void test_false_positive_traps() {
  g_test = "false_positive_traps";
  const std::vector<std::string> files = {
      "stage2/06_假阳性陷阱/06-01_段落中第三话不是章节.txt",
      "stage2/06_假阳性陷阱/06-02_数字开头句子.txt"};
  for (const auto& file : files) {
    const auto parsed = parse_file(file);
    CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(2));
    if (parsed.chapters.size() >= 2) {
      CHECK_EQ(parsed.chapters[0].title, std::string("第一章 正文"));
      CHECK_EQ(parsed.chapters[1].title, std::string("第二章 继续"));
    }
    CHECK(!contains_title(parsed, "第三话"));
    CHECK(!contains_title(parsed, "3个人"));
  }
}

void test_toc_region() {
  g_test = "toc_region";
  const auto parsed = parse_file("stage2/05_目录区与导航/05-01_开头目录区_标准.txt");
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(3));
  CHECK_EQ(parsed.toc_lines.size(), static_cast<std::size_t>(4));
  if (parsed.chapters.size() == 3) {
    CHECK_EQ(parsed.chapters[0].title, std::string("第一章 启程"));
    CHECK_EQ(parsed.chapters[2].title, std::string("第三章 离别"));
  }
}

void test_toc_duplicate_titles_hash() {
  g_test = "toc_duplicate_titles_hash";
  // 开头 3 条目录 + 后文同标题：靠「标题重现」判定目录区，正文 3 章必须保留。
  const std::string text =
      "第一章 启程\n第二章 相遇\n第三章 离别\n\n"
      "第一章 启程\n少年背起行囊。\n"
      "第二章 相遇\n他们在桥上相遇。\n"
      "第三章 离别\n终究还是要走。\n";
  const auto parsed = novel::parse_chapters(text);
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(3));
  CHECK_EQ(parsed.toc_lines.size(), static_cast<std::size_t>(3));
  if (parsed.chapters.size() == 3) {
    CHECK_EQ(parsed.chapters[0].title, std::string("第一章 启程"));
    CHECK_EQ(parsed.chapters[1].title, std::string("第二章 相遇"));
    CHECK_EQ(parsed.chapters[2].title, std::string("第三章 离别"));
    CHECK_EQ(parsed.chapters[0].start_line, static_cast<std::size_t>(4));
  }
  if (parsed.toc_lines.size() == 3) {
    CHECK_EQ(parsed.toc_lines[0], static_cast<std::size_t>(0));
    CHECK_EQ(parsed.toc_lines[2], static_cast<std::size_t>(2));
  }
  const std::string rendered =
      novel::render_txt(text, parsed.chapters, parsed.toc_lines);
  const auto lines = non_empty_lines(rendered);
  CHECK_EQ(lines.size(), static_cast<std::size_t>(6));
  CHECK(std::find(lines.begin(), lines.end(), std::string("第一章 启程")) !=
        lines.end());
  CHECK(std::find(lines.begin(), lines.end(), std::string("终究还是要走。")) !=
        lines.end());
}

void test_toc_duplicate_titles_hash_large() {
  g_test = "toc_duplicate_titles_hash_large";
  // 200 条目录 + 后文同标题：一次哈希扫描应正确切出目录区。
  constexpr std::size_t kCount = 200;
  std::string text;
  text.reserve(kCount * 48);
  for (std::size_t i = 1; i <= kCount; ++i) {
    text += "第" + std::to_string(i) + "章 标题" + std::to_string(i) + "\n";
  }
  text += "\n";
  for (std::size_t i = 1; i <= kCount; ++i) {
    text += "第" + std::to_string(i) + "章 标题" + std::to_string(i) + "\n";
    text += "正文内容。\n";
  }
  const auto parsed = novel::parse_chapters(text);
  CHECK_EQ(parsed.chapters.size(), kCount);
  CHECK_EQ(parsed.toc_lines.size(), kCount);
  if (parsed.chapters.size() == kCount) {
    CHECK_EQ(parsed.chapters.front().title, std::string("第1章 标题1"));
    CHECK_EQ(parsed.chapters.back().title,
             std::string("第200章 标题200"));
  }
}

void test_bracket_numbering() {
  g_test = "bracket_numbering";
  const auto parsed = parse_file("stage2/07_假阴性陷阱/07-05_括号编号.txt");
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(3));
}

void test_volume_levels() {
  g_test = "volume_levels";
  const auto parsed = parse_file("stage2/04_卷章层级结构/04-01_卷章两级_标准.txt");
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(3));
  if (parsed.chapters.size() == 3) {
    CHECK_EQ(parsed.chapters[0].title, std::string("第一卷"));
    CHECK_EQ(parsed.chapters[0].level, 1);
    CHECK_EQ(parsed.chapters[1].level, 2);
    CHECK_EQ(parsed.chapters[2].level, 2);
  }
}

void test_indented_title() {
  g_test = "indented_title";
  const auto parsed =
      parse_file("stage2/08_排版干扰标题识别/08-03_标题带全角空格缩进.txt");
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(3));
  if (parsed.chapters.size() == 3) {
    CHECK(parsed.chapters[1].title.find("第一章") != std::string::npos);
  }
}

void test_plain_novel_sample() {
  g_test = "plain_novel_sample";
  const auto parsed = parse_file("合成小说样本.txt");
  CHECK(!parsed.chapters.empty());
  CHECK(contains_title(parsed, "第1章 简介"));
  CHECK(contains_title(parsed, "第2章 幼年期 序章"));
}

void test_large_book_gb18030() {
  g_test = "large_book_gb18030";
  const auto loaded = load("real/large_gb18030.txt");
  CHECK(loaded.ok);
  CHECK_EQ(loaded.encoding, std::string("GB18030"));
  if (!loaded.ok) return;
  const auto parsed = novel::parse_chapters(loaded.text);
  // 合成前言 + 270 个编号章节。
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(271));
  if (!parsed.chapters.empty()) {
    const std::string* first = nullptr;
    for (const auto& chapter : parsed.chapters) {
      if (!chapter.synthetic) {
        first = &chapter.title;
        break;
      }
    }
    CHECK(first != nullptr);
    if (first != nullptr) CHECK(first->rfind("第1章", 0) == 0);
  }
  for (const auto& chapter : parsed.chapters) {
    const std::string& title = chapter.title;
    if (title.rfind("第", 0) != 0) continue;
    std::size_t i = 3;
    while (i < title.size() && title[i] >= '0' && title[i] <= '9') ++i;
    CHECK(title.compare(i, 3, "\xE5\xA4\xA9") != 0);  // 天
  }
}

void test_large_book_bom() {
  g_test = "large_book_bom";
  const auto loaded = load("real/large_utf8_bom.txt");
  CHECK(loaded.ok);
  CHECK(loaded.encoding.find("UTF-8") != std::string::npos);
  if (!loaded.ok) return;
  const auto parsed = novel::parse_chapters(loaded.text);
  // 合成前言 + 序章 + 346 个编号章节。
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(348));
  CHECK(contains_title(parsed, "序章"));
  CHECK(contains_title(parsed, "第1章 简介"));
  std::size_t duplicates = 0;
  for (const auto& chapter : parsed.chapters) {
    if (chapter.title == "第1章 简介") ++duplicates;
  }
  CHECK_EQ(duplicates, static_cast<std::size_t>(1));
}

void test_reorder_and_render() {
  g_test = "reorder_and_render";
  const auto loaded = load("stage2/09_综合与回归/09-01_完整小说_标准结构.txt");
  CHECK(loaded.ok);
  if (!loaded.ok) return;
  const auto parsed = novel::parse_chapters(loaded.text);

  const std::string identity =
      novel::render_txt(loaded.text, parsed.chapters, parsed.toc_lines);
  CHECK_EQ(non_empty_lines(identity), non_empty_lines(loaded.text));

  std::size_t afterword = 0;
  for (std::size_t i = 0; i < parsed.chapters.size(); ++i) {
    if (parsed.chapters[i].title == "后记") afterword = i + 1;
  }
  CHECK(afterword > 0);
  const auto ordered =
      novel::reorder_chapters(parsed.chapters,
                              {{static_cast<int>(afterword), 1}});
  CHECK(!ordered.empty());
  if (!ordered.empty()) CHECK_EQ(ordered.front().title, std::string("后记"));

  const std::string rendered =
      novel::render_txt(loaded.text, ordered, parsed.toc_lines);
  const auto rendered_lines = novel::split_lines(rendered);
  CHECK(!rendered_lines.empty());
  if (!rendered_lines.empty()) CHECK_EQ(rendered_lines.front(), std::string("后记"));

  auto original_set = non_empty_lines(loaded.text);
  auto rendered_set = non_empty_lines(rendered);
  std::sort(original_set.begin(), original_set.end());
  std::sort(rendered_set.begin(), rendered_set.end());
  CHECK_EQ(rendered_set, original_set);
}

void test_encoding_roundtrip() {
  g_test = "encoding_roundtrip";
  const auto loaded = load("stage2/09_综合与回归/09-01_完整小说_标准结构.txt");
  CHECK(loaded.ok);
  if (!loaded.ok) return;

  const auto plain = novel::decode_text_bytes(loaded.text);
  CHECK(plain.ok);
  CHECK_EQ(plain.text, loaded.text);
  CHECK_EQ(plain.encoding, std::string("UTF-8"));

  const std::string with_bom = "\xEF\xBB\xBF" + loaded.text;
  const auto bom = novel::decode_text_bytes(with_bom);
  CHECK(bom.ok);
  CHECK_EQ(bom.text, loaded.text);

#ifdef _WIN32
  const std::string gb = utf8_to_gb18030(loaded.text);
  CHECK(!gb.empty());
  const auto from_gb = novel::decode_text_bytes(gb);
  CHECK(from_gb.ok);
  CHECK_EQ(from_gb.encoding, std::string("GB18030"));
  CHECK_EQ(from_gb.text, loaded.text);
  CHECK(from_gb.warnings.empty());  // 正常 GB18030 不产生警告
#endif
}

void test_encoding_lossy_fallback() {
  g_test = "encoding_lossy_fallback";
  const std::string source = "第一章 测试\n正文。\n";

  // UTF-8 BOM 后只有一个坏字节：应容错打开并保留警告。
  {
    std::string bytes = "\xEF\xBB\xBF" + source;
    bytes.push_back(static_cast<char>(0x80));
    const auto decoded = novel::decode_text_bytes(bytes);
    CHECK(decoded.ok);
    CHECK_EQ(decoded.encoding, std::string("UTF-8 BOM（容错）"));
    CHECK(!decoded.warnings.empty());
    CHECK(decoded.text.find("\xEF\xBF\xBD") != std::string::npos);
  }

#ifdef _WIN32
  // GB18030 主体 + 一个坏字节：应替换为 U+FFFD，而不是整本拒绝。
  {
    std::string bytes = utf8_to_gb18030(source);
    CHECK(!bytes.empty());
    if (!bytes.empty()) {
      bytes.push_back(static_cast<char>(0x80));
      const auto decoded = novel::decode_text_bytes(bytes);
      CHECK(decoded.ok);
      CHECK_EQ(decoded.encoding, std::string("GB18030（容错）"));
      CHECK(!decoded.warnings.empty());
      CHECK(decoded.text.find("\xEF\xBF\xBD") != std::string::npos);
    }
  }

  // 大量坏字节仍拒绝，避免二进制文件被当小说打开。
  {
    const std::string garbage(1000, static_cast<char>(0x80));
    const auto decoded = novel::decode_text_bytes(garbage);
    CHECK(!decoded.ok);
  }
#endif
}

void test_empty_and_headerless() {
  g_test = "empty_and_headerless";
  const auto empty = novel::parse_chapters("");
  CHECK_EQ(empty.chapters.size(), static_cast<std::size_t>(1));
  if (!empty.chapters.empty()) {
    CHECK_EQ(empty.chapters[0].title, std::string("（全文）"));
    CHECK(empty.chapters[0].synthetic);
  }
  CHECK_EQ(empty.warnings.size(), static_cast<std::size_t>(1));

  const auto body_only =
      novel::parse_chapters("这是一段正文。\n\n又一段正文。\n");
  CHECK_EQ(body_only.chapters.size(), static_cast<std::size_t>(1));
  if (!body_only.chapters.empty()) {
    CHECK_EQ(body_only.chapters[0].title, std::string("（全文）"));
    CHECK(body_only.chapters[0].synthetic);
  }
  CHECK(!body_only.warnings.empty());

  const std::string rendered = novel::render_txt(
      "这是一段正文。\n\n又一段正文。\n", body_only.chapters, body_only.toc_lines);
  CHECK_EQ(non_empty_lines(rendered).size(), static_cast<std::size_t>(2));
}

void test_title_lines_option() {
  g_test = "title_lines_option";
  const auto defaults = parse_file("stage2/07_假阴性陷阱/07-01_无编号只有标题.txt");
  novel::ParseOptions options;
  options.accept_unnumbered_titles = true;
  const auto enabled =
      parse_file("stage2/07_假阴性陷阱/07-01_无编号只有标题.txt", options);
  CHECK(enabled.chapters.size() > defaults.chapters.size());
  CHECK_EQ(titles_of(enabled).size(), static_cast<std::size_t>(4));
  if (!enabled.chapters.empty()) {
    CHECK_EQ(enabled.chapters.front().title, std::string("启程"));
  }
}

void test_hash_prefix_without_space() {
  g_test = "hash_prefix_without_space";
  const auto parsed =
      parse_file("stage2/01_编号单位全覆盖/01-14_井号前缀_平台格式.txt");
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(3));
  if (parsed.chapters.size() == 3) {
    CHECK_EQ(parsed.chapters[0].title, std::string("第一章 启程"));
    CHECK_EQ(parsed.chapters[0].level, 2);
  }
}

void test_markdown_levels() {
  g_test = "markdown_levels";
  const auto outline = novel::parse_chapters(
      "# 第一卷 少年篇\n\n# 第一章 启程\n\n正文。\n");
  CHECK_EQ(outline.chapters.size(), static_cast<std::size_t>(2));
  if (outline.chapters.size() == 2) {
    CHECK_EQ(outline.chapters[0].level, 1);
    CHECK_EQ(outline.chapters[1].level, 2);
  }

  const auto sections = novel::parse_chapters(
      "# 第一章 启程\n\n## 1.1 小节\n\n正文。\n");
  CHECK_EQ(sections.chapters.size(), static_cast<std::size_t>(2));
  if (sections.chapters.size() == 2) {
    CHECK_EQ(sections.chapters[0].level, 2);
    CHECK_EQ(sections.chapters[1].level, 2);
  }
}

void test_spaced_numbering() {
  g_test = "spaced_numbering";
  const auto parsed =
      parse_file("stage2/01_编号单位全覆盖/01-17_数字带空格_第空格1空格章.txt");
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(3));
  if (parsed.chapters.size() == 3) {
    CHECK(parsed.chapters[0].title.find("启程") != std::string::npos);
    CHECK(parsed.chapters[2].title.find("离别") != std::string::npos);
  }
}

void test_bracketed_numbering() {
  g_test = "bracketed_numbering";
  const auto parsed =
      parse_file("stage2/01_编号单位全覆盖/01-20_编号带括号.txt");
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(3));
  if (!parsed.chapters.empty()) {
    CHECK(parsed.chapters[0].title.find("启程") != std::string::npos);
    CHECK_EQ(parsed.chapters[0].level, 2);
  }
}

void test_large_chinese_numerals() {
  g_test = "large_chinese_numerals";
  const auto parsed = parse_file("stage2/02_数字形式变体/02-03_汉字百千万.txt");
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(4));
  CHECK(contains_title(parsed, "第一万章"));
}

void test_prose_after_separator() {
  g_test = "prose_after_separator";
  const auto parsed = parse_file("stage2/06_假阳性陷阱/06-12_标题后有标点.txt");
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(2));
  CHECK(!contains_title(parsed, "这句话"));
}

void test_title_punctuation_regression() {
  g_test = "title_punctuation_regression";
  // 真实标题内部可以带感叹号。
  const auto exclaim = novel::parse_chapters("第139章 劝架！劝人吵架！\n\n正文。\n");
  CHECK_EQ(exclaim.chapters.size(), static_cast<std::size_t>(1));
  if (!exclaim.chapters.empty()) {
    CHECK_EQ(exclaim.chapters[0].title, std::string("第139章 劝架！劝人吵架！"));
  }

  const auto annotation =
      novel::parse_chapters("第270章 官宣！（全书完）\n\n正文。\n");
  CHECK_EQ(annotation.chapters.size(), static_cast<std::size_t>(1));

  // 编号标记不是句子边界。
  const auto fullwidth_dot = novel::parse_chapters("1．启程\n\n正文。\n");
  CHECK_EQ(fullwidth_dot.chapters.size(), static_cast<std::size_t>(1));

  // 「。」之后的第二句仍是正文。
  const auto prose = novel::parse_chapters(
      "第一章 正文\n\n第一章：启程。这句话出现在正文里。\n\n第二章 继续\n\n内容。\n");
  CHECK_EQ(prose.chapters.size(), static_cast<std::size_t>(2));
  CHECK(!contains_title(prose, "这句话"));
}

void test_false_positive_suite() {
  g_test = "false_positive_suite";
  const char* const files[] = {
      "stage2/06_假阳性陷阱/06-01_段落中第三话不是章节.txt",
      "stage2/06_假阳性陷阱/06-02_数字开头句子.txt",
      "stage2/06_假阳性陷阱/06-03_十年后非章节.txt",
      "stage2/06_假阳性陷阱/06-04_第5天非章节.txt",
      "stage2/06_假阳性陷阱/06-05_第一章三个字在句中.txt",
      "stage2/06_假阳性陷阱/06-06_对话中的第X章.txt",
      "stage2/06_假阳性陷阱/06-07_引用书目中的章名.txt",
      "stage2/06_假阳性陷阱/06-08_正文含序章二字.txt",
      "stage2/06_假阳性陷阱/06-09_短行非标题.txt",
      "stage2/06_假阳性陷阱/06-10_感叹号结尾.txt",
      "stage2/06_假阳性陷阱/06-11_句号结尾.txt",
      "stage2/06_假阳性陷阱/06-12_标题后有标点.txt",
      "stage2/06_假阳性陷阱/06-13_正文缩进含章号.txt",
      "stage2/06_假阳性陷阱/06-14_超长行含章号.txt",
      "stage2/06_假阳性陷阱/06-15_列表编号非章节.txt",
      "stage2/06_假阳性陷阱/06-16_日期中的章.txt",
      "stage2/06_假阳性陷阱/06-17_标题在段落中间.txt",
      "stage2/06_假阳性陷阱/06-18_伪章节标题_连续短行.txt",
  };
  for (const char* file : files) {
    const auto parsed = parse_file(file);
    if (parsed.chapters.size() != 2) {
      std::printf("  unexpected chapter count in %s: %zu\n", file,
                  parsed.chapters.size());
      for (const auto& chapter : parsed.chapters) {
        std::printf("    - %s\n", chapter.title.c_str());
      }
    }
    CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(2));
  }
}

void test_list_numbering_not_chapters() {
  g_test = "list_numbering_not_chapters";
  const auto list = parse_file("stage2/06_假阳性陷阱/06-15_列表编号非章节.txt");
  CHECK_EQ(list.chapters.size(), static_cast<std::size_t>(2));
  CHECK(!contains_title(list, "第一点"));
  CHECK(!list.warnings.empty());

  const auto plain = parse_file("stage2/01_编号单位全覆盖/01-13_纯数字编号.txt");
  CHECK_EQ(plain.chapters.size(), static_cast<std::size_t>(4));

  const auto bracket = parse_file("stage2/07_假阴性陷阱/07-05_括号编号.txt");
  CHECK_EQ(bracket.chapters.size(), static_cast<std::size_t>(3));
}

void test_title_lines_rejects_english_prose() {
  g_test = "title_lines_rejects_english_prose";
  novel::ParseOptions options;
  options.accept_unnumbered_titles = true;
  struct Expectation {
    const char* file;
    std::size_t count;
  };
  const Expectation expectations[] = {
      {"stage2/01_编号单位全覆盖/01-09_Chapter_X_英文.txt", 4},
      {"stage2/01_编号单位全覆盖/01-10_CHAPTER全大写.txt", 3},
      {"stage2/01_编号单位全覆盖/01-11_Ch点X_缩写.txt", 3},
      {"stage2/02_数字形式变体/02-09_罗马数字大写.txt", 4},
      {"stage2/07_假阴性陷阱/07-09_英文句点编号.txt", 3},
      {"stage2/09_综合与回归/09-04_完整小说_英文结构.txt", 5},
      {"stage2/10_判据专项补充/10-07_英文Volume_Part.txt", 5},
  };
  for (const auto& expectation : expectations) {
    const auto parsed = parse_file(expectation.file, options);
    if (parsed.chapters.size() != expectation.count) {
      std::printf("  unexpected chapter count in %s: %zu\n", expectation.file,
                  parsed.chapters.size());
      for (const auto& chapter : parsed.chapters) {
        std::printf("    - %s\n", chapter.title.c_str());
      }
    }
    CHECK_EQ(parsed.chapters.size(), expectation.count);
  }
}

// ===== 章节目录编辑 =====
int chapter_index_of(const novel::ParseResult& parsed,
                     const std::string& title) {
  for (std::size_t i = 0; i < parsed.chapters.size(); ++i) {
    if (parsed.chapters[i].title == title) return static_cast<int>(i);
  }
  return -1;
}

std::vector<std::string> sorted_lines(const std::vector<std::string>& lines) {
  std::vector<std::string> out = lines;
  std::sort(out.begin(), out.end());
  return out;
}

int line_index(const std::vector<std::string>& lines, const std::string& needle) {
  for (std::size_t i = 0; i < lines.size(); ++i) {
    if (lines[i] == needle) return static_cast<int>(i);
  }
  return -1;
}

void test_edit_rename() {
  g_test = "edit_rename";
  const auto loaded = load("stage2/09_综合与回归/09-01_完整小说_标准结构.txt");
  CHECK(loaded.ok);
  if (!loaded.ok) return;
  auto parsed = novel::parse_chapters(loaded.text);
  CHECK(parsed.chapters.size() > 2);
  if (parsed.chapters.size() <= 2) return;

  CHECK(novel::rename_chapter(parsed.chapters[1], "  第一章 新名字  "));
  CHECK(parsed.chapters[1].renamed);
  CHECK_EQ(parsed.chapters[1].title, std::string("第一章 新名字"));
  CHECK(!novel::rename_chapter(parsed.chapters[1], "   "));

  const std::string rendered =
      novel::render_txt(loaded.text, parsed.chapters, parsed.toc_lines);
  const auto before = non_empty_lines(loaded.text);
  const auto after = non_empty_lines(rendered);
  CHECK_EQ(after.size(), before.size());
  CHECK(line_index(after, "第一章 新名字") >= 0);
  CHECK(line_index(after, "第一章 启程") < 0);
  const int at = line_index(after, "第一章 新名字");
  if (at >= 0 && static_cast<std::size_t>(at + 1) < after.size()) {
    CHECK_EQ(after[static_cast<std::size_t>(at) + 1],
             std::string("少年背起行囊。"));
  }
}

void test_edit_level_markdown() {
  g_test = "edit_level_markdown";
  const std::string source =
      "# 第一章 启程\n\n## 1.1 小节\n\n正文。\n";
  auto parsed = novel::parse_chapters(source);
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(2));
  if (parsed.chapters.size() != 2) return;

  CHECK(novel::set_chapter_level(parsed.chapters[0], 1));
  CHECK(!novel::set_chapter_level(parsed.chapters[1], 3));
  CHECK(novel::set_chapter_level(parsed.chapters[1], 1));
  CHECK_EQ(non_empty_lines(
               novel::render_txt(source, parsed.chapters, parsed.toc_lines)),
           (std::vector<std::string>{"# 第一章 启程", "# 1.1 小节", "正文。"}));

  CHECK(novel::set_chapter_level(parsed.chapters[1], 2));
  CHECK_EQ(non_empty_lines(
               novel::render_txt(source, parsed.chapters, parsed.toc_lines)),
           (std::vector<std::string>{"# 第一章 启程", "## 1.1 小节", "正文。"}));

  CHECK(novel::rename_chapter(parsed.chapters[0], "第一章 出发"));
  const std::string rendered =
      novel::render_txt(source, parsed.chapters, parsed.toc_lines);
  CHECK_EQ(non_empty_lines(rendered),
           (std::vector<std::string>{"# 第一章 出发", "## 1.1 小节", "正文。"}));
}

void test_edit_merge_keep_heading() {
  g_test = "edit_merge_keep_heading";
  const auto loaded = load("stage2/09_综合与回归/09-01_完整小说_标准结构.txt");
  CHECK(loaded.ok);
  if (!loaded.ok) return;
  auto parsed = novel::parse_chapters(loaded.text);
  const std::size_t before = parsed.chapters.size();

  CHECK(novel::merge_with_previous(parsed.chapters, 1, false));
  CHECK(!novel::merge_with_previous(parsed.chapters, 0, false));
  CHECK_EQ(parsed.chapters.size(), before - 1);
  CHECK_EQ(parsed.chapters[0].index, 0);

  const std::string rendered =
      novel::render_txt(loaded.text, parsed.chapters, parsed.toc_lines);
  const auto after = non_empty_lines(rendered);
  CHECK(line_index(after, "第一章 启程") >= 0);
  CHECK(line_index(after, "少年背起行囊。") >= 0);
  CHECK_EQ(sorted_lines(after), sorted_lines(non_empty_lines(loaded.text)));
}

void test_edit_merge_suppress() {
  g_test = "edit_merge_suppress";
  const auto loaded = load("stage2/09_综合与回归/09-01_完整小说_标准结构.txt");
  CHECK(loaded.ok);
  if (!loaded.ok) return;
  auto parsed = novel::parse_chapters(loaded.text);

  CHECK(novel::merge_with_previous(parsed.chapters, 1, true));
  const std::string rendered =
      novel::render_txt(loaded.text, parsed.chapters, parsed.toc_lines);
  const auto after = non_empty_lines(rendered);
  CHECK(line_index(after, "第一章 启程") < 0);
  CHECK(line_index(after, "少年背起行囊。") >= 0);
  CHECK_EQ(after.size(), non_empty_lines(loaded.text).size() - 1);
}

void test_edit_merge_chain_suppress() {
  g_test = "edit_merge_chain_suppress";
  const auto loaded = load("stage2/09_综合与回归/09-01_完整小说_标准结构.txt");
  CHECK(loaded.ok);
  if (!loaded.ok) return;
  auto parsed = novel::parse_chapters(loaded.text);
  CHECK(parsed.chapters.size() > 3);
  if (parsed.chapters.size() <= 3) return;

  const std::size_t before = parsed.chapters.size();
  // 连续把 3 个后续章并入首章并丢弃标题。
  CHECK(novel::merge_with_previous(parsed.chapters, 1, true));
  CHECK(novel::merge_with_previous(parsed.chapters, 1, true));
  CHECK(novel::merge_with_previous(parsed.chapters, 1, true));
  CHECK_EQ(parsed.chapters.size(), before - 3);

  const std::string rendered =
      novel::render_txt(loaded.text, parsed.chapters, parsed.toc_lines);
  const auto after = non_empty_lines(rendered);
  CHECK(line_index(after, "第一章 启程") < 0);
  CHECK(line_index(after, "第二章 相遇") < 0);
  CHECK(line_index(after, "第三章 离别") < 0);
  CHECK(line_index(after, "少年背起行囊。") >= 0);
  CHECK(line_index(after, "他们在桥上相遇。") >= 0);
  CHECK(line_index(after, "终究还是要走。") >= 0);
  CHECK_EQ(after.size(), non_empty_lines(loaded.text).size() - 3);

  CHECK_EQ(parsed.chapters[0].skip_lines.size(), static_cast<std::size_t>(3));
  for (const auto& chapter : parsed.chapters) {
    for (std::size_t i = 1; i < chapter.skip_lines.size(); ++i) {
      CHECK(chapter.skip_lines[i - 1] < chapter.skip_lines[i]);
    }
  }
}

void test_edit_delete() {
  g_test = "edit_delete";
  const auto loaded = load("stage2/09_综合与回归/09-01_完整小说_标准结构.txt");
  CHECK(loaded.ok);
  if (!loaded.ok) return;
  auto parsed = novel::parse_chapters(loaded.text);
  const std::size_t before = parsed.chapters.size();

  std::vector<novel::LineRange> excluded;
  CHECK(novel::delete_chapter(parsed.chapters, 1, excluded));
  CHECK(!novel::delete_chapter(parsed.chapters, 99, excluded));
  CHECK_EQ(parsed.chapters.size(), before - 1);
  CHECK_EQ(excluded.size(), static_cast<std::size_t>(1));

  const std::string rendered =
      novel::render_txt(loaded.text, parsed.chapters, parsed.toc_lines, excluded);
  const auto after = non_empty_lines(rendered);
  CHECK(line_index(after, "第一章 启程") < 0);
  CHECK(line_index(after, "少年背起行囊。") < 0);
  CHECK(line_index(after, "第二章 相遇") >= 0);

  auto expected = non_empty_lines(loaded.text);
  expected.erase(std::remove(expected.begin(), expected.end(),
                             std::string("第一章 启程")),
                 expected.end());
  expected.erase(std::remove(expected.begin(), expected.end(),
                             std::string("少年背起行囊。")),
                 expected.end());
  CHECK_EQ(sorted_lines(after), sorted_lines(expected));
}

void test_edit_split() {
  g_test = "edit_split";
  const auto loaded = load("stage2/09_综合与回归/09-01_完整小说_标准结构.txt");
  CHECK(loaded.ok);
  if (!loaded.ok) return;
  auto parsed = novel::parse_chapters(loaded.text);
  const std::size_t before = parsed.chapters.size();

  const int index = chapter_index_of(parsed, "后记");
  CHECK(index >= 0);
  if (index < 0) return;
  const std::size_t start = parsed.chapters[static_cast<std::size_t>(index)].start_line;
  const std::size_t split_at = start + 4;  // 第 2 个正文行
  const std::vector<std::string> source_lines = novel::split_lines(loaded.text);
  CHECK(static_cast<std::size_t>(split_at) < source_lines.size());
  if (static_cast<std::size_t>(split_at) >= source_lines.size()) return;
  const std::string second_body = source_lines[split_at];

  CHECK(!novel::split_chapter(parsed.chapters, index,
                              parsed.chapters[static_cast<std::size_t>(index)]
                                  .start_line,
                              "第9章 新章", 2));
  CHECK(!novel::split_chapter(parsed.chapters, index, split_at, "  ", 2));
  CHECK(!novel::split_chapter(parsed.chapters, index, split_at, "第9章 新章", 0));
  CHECK(novel::split_chapter(parsed.chapters, index, split_at, "第9章 新章", 2));
  CHECK_EQ(parsed.chapters.size(), before + 1);
  CHECK(parsed.chapters[static_cast<std::size_t>(index) + 1].inserted);
  CHECK_EQ(parsed.chapters[static_cast<std::size_t>(index)].end_line, split_at);

  const std::string rendered =
      novel::render_txt(loaded.text, parsed.chapters, parsed.toc_lines);
  const auto after = non_empty_lines(rendered);
  const int title_at = line_index(after, "第9章 新章");
  const int body_at = line_index(after, second_body);
  CHECK(title_at >= 0);
  CHECK(body_at >= 0);
  CHECK(title_at >= 0 && body_at >= 0 && title_at < body_at);

  auto expected = non_empty_lines(loaded.text);
  expected.push_back("第9章 新章");
  CHECK_EQ(sorted_lines(after), sorted_lines(expected));
}

void test_edit_append() {
  g_test = "edit_append";
  const auto loaded = load("stage2/09_综合与回归/09-01_完整小说_标准结构.txt");
  CHECK(loaded.ok);
  if (!loaded.ok) return;
  auto parsed = novel::parse_chapters(loaded.text);
  const std::size_t before = parsed.chapters.size();
  const std::size_t total_lines = novel::split_lines(loaded.text).size();

  CHECK(!novel::append_chapter(parsed.chapters, total_lines, "", 2));
  CHECK(novel::append_chapter(parsed.chapters, total_lines, "第99章 终章", 2));
  CHECK_EQ(parsed.chapters.size(), before + 1);
  CHECK(parsed.chapters.back().inserted);

  const std::string rendered =
      novel::render_txt(loaded.text, parsed.chapters, parsed.toc_lines);
  const auto after = non_empty_lines(rendered);
  CHECK(!after.empty());
  if (!after.empty()) CHECK_EQ(after.back(), std::string("第99章 终章"));
  CHECK_EQ(after.size(), non_empty_lines(loaded.text).size() + 1);

  // 插入章的编号随 renumber 同步（heading_line 与 title 一起更新）。
  CHECK(novel::renumber_chapters(parsed.chapters, 1, false));
  CHECK_EQ(parsed.chapters.back().title, std::string("第4章 终章"));
  CHECK_EQ(parsed.chapters.back().heading_line, std::string("第4章 终章"));
  const auto renumbered =
      non_empty_lines(novel::render_txt(loaded.text, parsed.chapters,
                                        parsed.toc_lines));
  CHECK(!renumbered.empty());
  if (!renumbered.empty()) CHECK_EQ(renumbered.back(), std::string("第4章 终章"));

  // 带井号的插入标题：改层级与编号时保留井号风格。
  CHECK(novel::append_chapter(parsed.chapters, total_lines, "## 第99节 附录", 2));
  CHECK_EQ(parsed.chapters.back().title, std::string("第99节 附录"));
  CHECK_EQ(parsed.chapters.back().heading_line, std::string("## 第99节 附录"));
  CHECK(novel::set_chapter_level(parsed.chapters.back(), 1));
  CHECK_EQ(parsed.chapters.back().heading_line, std::string("# 第99节 附录"));
  CHECK(novel::renumber_chapters(parsed.chapters, 1, false));
  CHECK_EQ(parsed.chapters.back().heading_line, std::string("# 第1节 附录"));
}

void test_edit_renumber_arabic() {
  g_test = "edit_renumber_arabic";
  const auto loaded = load("stage2/09_综合与回归/09-01_完整小说_标准结构.txt");
  CHECK(loaded.ok);
  if (!loaded.ok) return;
  auto parsed = novel::parse_chapters(loaded.text);

  CHECK(!novel::renumber_chapters(parsed.chapters, 0, false));
  CHECK(novel::renumber_chapters(parsed.chapters, 1, false));
  CHECK_EQ(titles_of(parsed),
           (std::vector<std::string>{"序章", "第1章 启程", "第2章 相遇",
                                     "第3章 离别", "终章", "后记"}));

  const std::string rendered =
      novel::render_txt(loaded.text, parsed.chapters, parsed.toc_lines);
  const auto after = non_empty_lines(rendered);
  CHECK(line_index(after, "第1章 启程") >= 0);
  CHECK(line_index(after, "第3章 离别") >= 0);
  CHECK(line_index(after, "序章") >= 0);
  CHECK(line_index(after, "终章") >= 0);
  CHECK(line_index(after, "后记") >= 0);
}

void test_edit_renumber_chinese() {
  g_test = "edit_renumber_chinese";
  const auto parsed_source = parse_file("stage2/02_数字形式变体/02-01_汉字一到十.txt");
  CHECK_EQ(parsed_source.chapters.size(), static_cast<std::size_t>(10));
  if (parsed_source.chapters.size() != 10) return;

  auto parsed = parsed_source;
  CHECK(novel::renumber_chapters(parsed.chapters, 1, true));
  CHECK_EQ(parsed.chapters[0].title, std::string("第一章 一"));
  CHECK_EQ(parsed.chapters[8].title, std::string("第九章 九"));
  CHECK_EQ(parsed.chapters[9].title, std::string("第十章 十"));

  auto shifted = parsed_source;
  CHECK(novel::renumber_chapters(shifted.chapters, 10, true));
  CHECK_EQ(shifted.chapters[0].title, std::string("第十章 一"));
  CHECK_EQ(shifted.chapters[1].title, std::string("第十一章 二"));
  CHECK_EQ(shifted.chapters[9].title, std::string("第十九章 十"));

  // 序章/终章/后记这类无编号标题原样保留。
  const auto standard = load("stage2/09_综合与回归/09-01_完整小说_标准结构.txt");
  CHECK(standard.ok);
  if (!standard.ok) return;
  auto mixed = novel::parse_chapters(standard.text);
  CHECK(novel::renumber_chapters(mixed.chapters, 1, true));
  CHECK_EQ(mixed.chapters[0].title, std::string("序章"));
  CHECK_EQ(mixed.chapters[1].title, std::string("第一章 启程"));
  CHECK_EQ(mixed.chapters[4].title, std::string("终章"));
  CHECK_EQ(mixed.chapters[5].title, std::string("后记"));
}

void test_edit_delete_reorder_render() {
  g_test = "edit_delete_reorder_render";
  const auto loaded = load("stage2/09_综合与回归/09-01_完整小说_标准结构.txt");
  CHECK(loaded.ok);
  if (!loaded.ok) return;
  auto parsed = novel::parse_chapters(loaded.text);

  std::vector<novel::LineRange> excluded;
  CHECK(novel::delete_chapter(parsed.chapters, 2, excluded));
  const int count = static_cast<int>(parsed.chapters.size());

  auto ordered = novel::reorder_chapters(parsed.chapters, {{count, 1}});
  CHECK(!ordered.empty());
  if (ordered.empty()) return;
  CHECK_EQ(ordered.front().title, std::string("后记"));
  CHECK_EQ(ordered.back().index, count - 1);

  const std::string rendered =
      novel::render_txt(loaded.text, ordered, parsed.toc_lines, excluded);
  const auto after = non_empty_lines(rendered);
  CHECK_EQ(line_index(after, "第二章 相遇") < 0, true);
  CHECK_EQ(line_index(after, "他们在桥上相遇。") < 0, true);
  CHECK_EQ(after.front(), std::string("后记"));

  auto expected = non_empty_lines(loaded.text);
  expected.erase(std::remove(expected.begin(), expected.end(),
                             std::string("第二章 相遇")),
                 expected.end());
  expected.erase(std::remove(expected.begin(), expected.end(),
                             std::string("他们在桥上相遇。")),
                 expected.end());
  CHECK_EQ(sorted_lines(after), sorted_lines(expected));
}

novel::TextExclusion exclusion_of(std::size_t line, const std::string& line_text,
                                  const std::string& needle) {
  const std::size_t begin = line_text.find(needle);
  return novel::TextExclusion{line, begin, begin + needle.size()};
}

void test_invalid_content_partial_line() {
  g_test = "invalid_content_partial_line";
  const std::string text =
      "第1章 开始\n这是广告正文。\n第二行内容。\n\n第2章 后续\n尾部内容。\n";
  const auto parsed = novel::parse_chapters(text);
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(2));
  if (parsed.chapters.size() != 2) return;
  const std::vector<std::string> lines = novel::split_lines(text);
  const std::vector<novel::TextExclusion> exclusions = {
      exclusion_of(1, lines[1], "广告")};

  const std::string rendered = novel::render_txt(
      text, parsed.chapters, parsed.toc_lines, {}, exclusions);
  CHECK(rendered.find("广告") == std::string::npos);
  const auto after = non_empty_lines(rendered);
  CHECK_EQ(after.size(), static_cast<std::size_t>(5));
  if (after.size() == 5) {
    CHECK_EQ(after[1], std::string("这是正文。"));
    CHECK_EQ(after[2], std::string("第二行内容。"));
    CHECK_EQ(after[4], std::string("尾部内容。"));
  }
}

void test_invalid_content_multi_line() {
  g_test = "invalid_content_multi_line";
  const std::string text =
      "第1章 开始\n这是广告正文。\n第二行内容。\n\n第2章 后续\n尾部内容。\n";
  const auto parsed = novel::parse_chapters(text);
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(2));
  if (parsed.chapters.size() != 2) return;
  const std::vector<std::string> lines = novel::split_lines(text);
  const std::vector<novel::TextExclusion> exclusions = {
      exclusion_of(1, lines[1], "广告"), exclusion_of(2, lines[2], "第二")};

  const std::string rendered = novel::render_txt(
      text, parsed.chapters, parsed.toc_lines, {}, exclusions);
  const auto after = non_empty_lines(rendered);
  CHECK_EQ(after.size(), static_cast<std::size_t>(5));
  if (after.size() == 5) {
    CHECK_EQ(after[1], std::string("这是正文。"));
    CHECK_EQ(after[2], std::string("行内容。"));
  }
}

void test_invalid_content_whole_line() {
  g_test = "invalid_content_whole_line";
  const std::string text =
      "第1章 开始\n这是广告正文。\n第二行内容。\n尾部内容。\n";
  const auto parsed = novel::parse_chapters(text);
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(1));
  if (parsed.chapters.size() != 1) return;
  const std::vector<std::string> lines = novel::split_lines(text);
  const std::vector<novel::TextExclusion> exclusions = {
      novel::TextExclusion{2, 0, lines[2].size()}};

  const std::string rendered = novel::render_txt(
      text, parsed.chapters, parsed.toc_lines, {}, exclusions);
  CHECK(rendered.find("第二行内容。") == std::string::npos);
  const auto after = novel::split_lines(rendered);
  CHECK_EQ(after.size(), static_cast<std::size_t>(3));
  if (after.size() == 3) {
    CHECK_EQ(after[0], std::string("第1章 开始"));
    CHECK_EQ(after[1], std::string("这是广告正文。"));
    CHECK_EQ(after[2], std::string("尾部内容。"));
  }
}

void test_invalid_content_overlap_merge() {
  g_test = "invalid_content_overlap_merge";
  const std::string text = "第1章 开始\n这是广告正文。\n";
  const auto parsed = novel::parse_chapters(text);
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(1));
  if (parsed.chapters.size() != 1) return;
  // [0,6) 与 [3,9) 重叠，应合并为 [0,9)，只删一次「这是广」。
  const std::vector<novel::TextExclusion> exclusions = {
      novel::TextExclusion{1, 0, 6}, novel::TextExclusion{1, 3, 9}};
  const std::string rendered = novel::render_txt(
      text, parsed.chapters, parsed.toc_lines, {}, exclusions);
  const auto after = non_empty_lines(rendered);
  CHECK_EQ(after.size(), static_cast<std::size_t>(2));
  if (after.size() == 2) CHECK_EQ(after[1], std::string("告正文。"));
  CHECK(rendered.find("这是广") == std::string::npos);

  // 相邻区间 [0,3) 与 [3,6) 同样合并为一次删除。
  const std::vector<novel::TextExclusion> adjacent = {
      novel::TextExclusion{1, 0, 3}, novel::TextExclusion{1, 3, 6}};
  const std::string merged = novel::render_txt(
      text, parsed.chapters, parsed.toc_lines, {}, adjacent);
  const auto merged_lines = non_empty_lines(merged);
  CHECK_EQ(merged_lines.size(), static_cast<std::size_t>(2));
  if (merged_lines.size() == 2) {
    CHECK_EQ(merged_lines[1], std::string("广告正文。"));
  }
}

void test_invalid_content_utf8_boundary() {
  g_test = "invalid_content_utf8_boundary";
  const std::string text = "第1章 开始\n这是广告正文。\n";
  const auto parsed = novel::parse_chapters(text);
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(1));
  if (parsed.chapters.size() != 1) return;
  // [1,7) 起点落在「这」中间、终点落在「广」中间，收缩为 [3,6) 只删「是」。
  const std::vector<novel::TextExclusion> exclusions = {
      novel::TextExclusion{1, 1, 7}};
  const std::string rendered = novel::render_txt(
      text, parsed.chapters, parsed.toc_lines, {}, exclusions);
  CHECK(novel::is_valid_utf8(rendered));
  const auto after = non_empty_lines(rendered);
  CHECK_EQ(after.size(), static_cast<std::size_t>(2));
  if (after.size() == 2) CHECK_EQ(after[1], std::string("这广告正文。"));
}

void test_invalid_content_with_edits() {
  g_test = "invalid_content_with_edits";
  const std::string text =
      "第1章 甲\n第一段甲。\n第二段甲。\n\n"
      "第2章 乙\n第一段乙。\n第二段乙。\n\n"
      "第3章 丙\n第一段丙。\n";
  auto parsed = novel::parse_chapters(text);
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(3));
  if (parsed.chapters.size() != 3) return;
  const std::vector<std::string> lines = novel::split_lines(text);
  CHECK_EQ(lines.size(), static_cast<std::size_t>(10));
  if (lines.size() != 10) return;

  CHECK(novel::rename_chapter(parsed.chapters[0], "第一章 改名"));
  CHECK(novel::merge_with_previous(parsed.chapters, 2, true));
  CHECK(novel::split_chapter(parsed.chapters, 0, 2, "第9章 插入", 2));
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(3));

  const std::vector<novel::TextExclusion> exclusions = {
      novel::TextExclusion{0, 0, lines[0].size()},  // 改名标题忽略映射
      exclusion_of(6, lines[6], "乙"),
      exclusion_of(9, lines[9], "丙"),
  };
  const std::string rendered = novel::render_txt(
      text, parsed.chapters, parsed.toc_lines, {}, exclusions);
  CHECK(novel::is_valid_utf8(rendered));
  CHECK(rendered.find("第一章 改名") != std::string::npos);
  CHECK(rendered.find("第3章 丙") == std::string::npos);
  CHECK(rendered.find("第二段乙") == std::string::npos);
  CHECK(rendered.find("第一段丙") == std::string::npos);

  const auto after = non_empty_lines(rendered);
  const std::vector<std::string> expected = {
      "第一章 改名", "第一段甲。", "第9章 插入", "第二段甲。",
      "第2章 乙",   "第一段乙。", "第二段。",   "第一段。"};
  CHECK_EQ(after, expected);

  // 默认空 exclusions 与旧四参调用逐字节一致。
  const std::string legacy =
      novel::render_txt(text, parsed.chapters, parsed.toc_lines);
  const std::string explicit_empty = novel::render_txt(
      text, parsed.chapters, parsed.toc_lines, {}, {});
  CHECK_EQ(legacy, explicit_empty);
  CHECK(legacy.find("第二段乙。") != std::string::npos);
}

// ===== 章节序号列 / 标题列 =====
void test_columns_parse_split() {
  g_test = "columns_parse_split";
  const std::string text =
      "第1章 鞋跟先生！\n\n正文一。\n\n"
      "第2章鞋跟先生！\n\n正文二。\n\n"
      "序章\n\n正文三。\n\n"
      "番外一 日常\n\n正文四。\n\n"
      "Chapter 1 The Beginning\n\n正文五。\n\n"
      "1. 启程\n\n正文六。\n";
  const auto parsed = novel::parse_chapters(text);
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(6));
  if (parsed.chapters.size() != 6) {
    for (const auto& chapter : parsed.chapters) {
      std::printf("  - %s\n", chapter.title.c_str());
    }
    return;
  }

  CHECK_EQ(parsed.chapters[0].number, std::string("第1章"));
  CHECK_EQ(parsed.chapters[0].separator, std::string(" "));
  CHECK_EQ(parsed.chapters[0].name, std::string("鞋跟先生！"));
  CHECK(parsed.chapters[0].numbered);

  CHECK_EQ(parsed.chapters[1].number, std::string("第2章"));
  CHECK_EQ(parsed.chapters[1].separator, std::string(""));
  CHECK_EQ(parsed.chapters[1].name, std::string("鞋跟先生！"));
  CHECK(parsed.chapters[1].numbered);

  CHECK_EQ(parsed.chapters[2].number, std::string("序章"));
  CHECK_EQ(parsed.chapters[2].separator, std::string(""));
  CHECK_EQ(parsed.chapters[2].name, std::string(""));
  CHECK(!parsed.chapters[2].numbered);

  CHECK_EQ(parsed.chapters[3].number, std::string("番外一"));
  CHECK_EQ(parsed.chapters[3].separator, std::string(" "));
  CHECK_EQ(parsed.chapters[3].name, std::string("日常"));
  CHECK(!parsed.chapters[3].numbered);

  CHECK_EQ(parsed.chapters[4].number, std::string("Chapter 1"));
  CHECK_EQ(parsed.chapters[4].separator, std::string(" "));
  CHECK_EQ(parsed.chapters[4].name, std::string("The Beginning"));
  CHECK(parsed.chapters[4].numbered);

  CHECK_EQ(parsed.chapters[5].number, std::string("1."));
  CHECK_EQ(parsed.chapters[5].separator, std::string(" "));
  CHECK_EQ(parsed.chapters[5].name, std::string("启程"));
  CHECK(parsed.chapters[5].numbered);

  // 两列拼回去必须等于完整标题（title 不变）。
  for (const auto& chapter : parsed.chapters) {
    CHECK_EQ(chapter.number + chapter.separator + chapter.name, chapter.title);
  }

  // 括号编号同样拆列。
  const auto bracket = novel::parse_chapters("(2) 归途\n\n正文。\n");
  CHECK_EQ(bracket.chapters.size(), static_cast<std::size_t>(1));
  if (!bracket.chapters.empty()) {
    CHECK_EQ(bracket.chapters[0].number, std::string("(2)"));
    CHECK_EQ(bracket.chapters[0].name, std::string("归途"));
    CHECK(bracket.chapters[0].numbered);
  }
}

void test_columns_false_positive_06() {
  g_test = "columns_false_positive_06";
  const char* files[] = {
      "stage2/06_假阳性陷阱/06-01_段落中第三话不是章节.txt",
      "stage2/06_假阳性陷阱/06-02_数字开头句子.txt",
      "stage2/06_假阳性陷阱/06-03_十年后非章节.txt",
      "stage2/06_假阳性陷阱/06-04_第5天非章节.txt",
      "stage2/06_假阳性陷阱/06-05_第一章三个字在句中.txt",
      "stage2/06_假阳性陷阱/06-06_对话中的第X章.txt",
      "stage2/06_假阳性陷阱/06-07_引用书目中的章名.txt",
      "stage2/06_假阳性陷阱/06-08_正文含序章二字.txt",
      "stage2/06_假阳性陷阱/06-09_短行非标题.txt",
      "stage2/06_假阳性陷阱/06-10_感叹号结尾.txt",
      "stage2/06_假阳性陷阱/06-11_句号结尾.txt",
      "stage2/06_假阳性陷阱/06-12_标题后有标点.txt",
      "stage2/06_假阳性陷阱/06-13_正文缩进含章号.txt",
      "stage2/06_假阳性陷阱/06-14_超长行含章号.txt",
      "stage2/06_假阳性陷阱/06-15_列表编号非章节.txt",
      "stage2/06_假阳性陷阱/06-16_日期中的章.txt",
      "stage2/06_假阳性陷阱/06-17_标题在段落中间.txt",
      "stage2/06_假阳性陷阱/06-18_伪章节标题_连续短行.txt",
  };
  for (const char* file : files) {
    const auto parsed = parse_file(file);
    if (parsed.chapters.size() != 2) {
      std::printf("  unexpected count in %s: %zu\n", file,
                  parsed.chapters.size());
      for (const auto& chapter : parsed.chapters) {
        std::printf("    - %s\n", chapter.title.c_str());
      }
    }
    CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(2));
  }

  const auto parsed = parse_file("stage2/06_假阳性陷阱/06-10_感叹号结尾.txt");
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(2));
  if (parsed.chapters.size() == 2) {
    CHECK_EQ(parsed.chapters[0].title, std::string("第一章 正文"));
    CHECK_EQ(parsed.chapters[1].title, std::string("第二章 继续"));
    CHECK_EQ(parsed.chapters[0].number, std::string("第一章"));
    CHECK_EQ(parsed.chapters[0].name, std::string("正文"));
    CHECK(parsed.chapters[0].numbered);
  }
  CHECK(!contains_title(parsed, "第一章真好看！"));

  // 中文数字 + 感叹号不加空格仍是段落；带逗号的更不是标题。
  const auto lone = novel::parse_chapters("第一章真好看！\n");
  CHECK_EQ(lone.chapters.size(), static_cast<std::size_t>(1));
  CHECK(lone.chapters[0].synthetic);
  const auto comma =
      novel::parse_chapters("第3章的内容我看过了，非常精彩！\n");
  CHECK_EQ(comma.chapters.size(), static_cast<std::size_t>(1));
  CHECK(comma.chapters[0].synthetic);
}

void test_columns_rename_parts() {
  g_test = "columns_rename_parts";
  const auto loaded = load("stage2/09_综合与回归/09-01_完整小说_标准结构.txt");
  CHECK(loaded.ok);
  if (!loaded.ok) return;
  auto parsed = novel::parse_chapters(loaded.text);
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(6));
  if (parsed.chapters.size() != 6) return;

  novel::Chapter& chapter = parsed.chapters[1];
  CHECK(novel::rename_chapter_parts(chapter, "第5章", "新名"));
  CHECK_EQ(chapter.number, std::string("第5章"));
  CHECK_EQ(chapter.name, std::string("新名"));
  CHECK_EQ(chapter.separator, std::string(" "));
  CHECK_EQ(chapter.title, std::string("第5章 新名"));
  CHECK(chapter.numbered);

  CHECK(novel::rename_chapter_parts(chapter, "序章", " "));
  CHECK_EQ(chapter.title, std::string("序章"));
  CHECK_EQ(chapter.name, std::string(""));
  CHECK_EQ(chapter.separator, std::string(""));
  CHECK(!chapter.numbered);

  CHECK(novel::rename_chapter_parts(chapter, "", "孤名"));
  CHECK_EQ(chapter.title, std::string("孤名"));
  CHECK(!chapter.numbered);

  const std::string saved = chapter.title;
  CHECK(!novel::rename_chapter_parts(chapter, "  ", "  "));
  CHECK_EQ(chapter.title, saved);

  // 老 API 改名后两列同步，不再与 title 脱节。
  CHECK(novel::rename_chapter(chapter, "第7章 旧接口"));
  CHECK_EQ(chapter.number, std::string("第7章"));
  CHECK_EQ(chapter.name, std::string("旧接口"));
  CHECK(chapter.numbered);

  CHECK(novel::rename_chapter_parts(chapter, "Chapter 9", "尾声之前"));
  CHECK_EQ(chapter.title, std::string("Chapter 9 尾声之前"));
  CHECK(chapter.numbered);

  const std::string rendered =
      novel::render_txt(loaded.text, parsed.chapters, parsed.toc_lines);
  CHECK_EQ(line_index(non_empty_lines(rendered), "Chapter 9 尾声之前"),
           line_index(non_empty_lines(loaded.text), "第一章 启程"));
}

void test_columns_renumber_pad() {
  g_test = "columns_renumber_pad";
  const auto parsed_source = parse_file(
      "stage2/01_编号单位全覆盖/01-02_第X章_阿拉伯数字.txt");
  CHECK_EQ(parsed_source.chapters.size(), static_cast<std::size_t>(4));
  if (parsed_source.chapters.size() != 4) return;

  auto parsed = parsed_source;
  novel::set_excluded_from_numbering(parsed.chapters[1], true);
  CHECK(parsed.chapters[1].excluded_from_numbering);
  CHECK(novel::renumber_chapters(parsed.chapters, 1, false, 3));
  CHECK_EQ(parsed.chapters[0].number, std::string("第001章"));
  CHECK_EQ(parsed.chapters[0].name, std::string("起点"));
  CHECK_EQ(parsed.chapters[0].title, std::string("第001章 起点"));
  CHECK_EQ(parsed.chapters[1].title, std::string("第2章 转折"));  // 排除：不占号
  CHECK_EQ(parsed.chapters[2].number, std::string("第002章"));
  CHECK_EQ(parsed.chapters[2].title, std::string("第002章 高潮"));
  CHECK_EQ(parsed.chapters[3].number, std::string("第003章"));
  CHECK_EQ(parsed.chapters[3].title, std::string("第003章 结局"));

  // 取消排除后重新占号。
  novel::set_excluded_from_numbering(parsed.chapters[1], false);
  CHECK(novel::renumber_chapters(parsed.chapters, 1, false, 3));
  CHECK_EQ(parsed.chapters[1].title, std::string("第002章 转折"));
  CHECK_EQ(parsed.chapters[3].title, std::string("第004章 结局"));

  // 中文数字忽略 pad。
  auto chinese = parsed_source;
  CHECK(novel::renumber_chapters(chinese.chapters, 1, true, 3));
  CHECK_EQ(chinese.chapters[0].title, std::string("第一章 起点"));
  CHECK_EQ(chinese.chapters[3].title, std::string("第四章 结局"));

  // 序章/终章/后记这类无编号标题不占号、不改写。
  const auto standard = load("stage2/09_综合与回归/09-01_完整小说_标准结构.txt");
  CHECK(standard.ok);
  if (standard.ok) {
    auto mixed = novel::parse_chapters(standard.text);
    CHECK(novel::renumber_chapters(mixed.chapters, 1, false, 3));
    CHECK_EQ(mixed.chapters[0].title, std::string("序章"));
    CHECK_EQ(mixed.chapters[1].title, std::string("第001章 启程"));
    CHECK_EQ(mixed.chapters[2].title, std::string("第002章 相遇"));
    CHECK_EQ(mixed.chapters[4].title, std::string("终章"));
    CHECK_EQ(mixed.chapters[5].title, std::string("后记"));
  }

  auto rejected = parsed_source;
  CHECK(!novel::renumber_chapters(rejected.chapters, 1, false, -1));
  CHECK(!novel::renumber_chapters(rejected.chapters, 0, false, 3));
  CHECK_EQ(titles_of(rejected), titles_of(parsed_source));
}

void test_columns_split_numbered() {
  g_test = "columns_split_numbered";
  const auto loaded = load("stage2/09_综合与回归/09-01_完整小说_标准结构.txt");
  CHECK(loaded.ok);
  if (!loaded.ok) return;
  auto parsed = novel::parse_chapters(loaded.text);
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(6));
  if (parsed.chapters.size() != 6) return;

  // 边界：层级非法、游标不在正文、无编号章的 number/name 同时为空。
  CHECK(!novel::split_chapter_numbered(parsed.chapters, 2, 10, "", "x", 3));
  CHECK(!novel::split_chapter_numbered(parsed.chapters, 2, 8, "", "x", 2));
  CHECK(!novel::split_chapter_numbered(parsed.chapters, 0, 2, "", "", 2));

  const std::size_t second_start = parsed.chapters[2].start_line;
  const std::size_t second_end = parsed.chapters[2].end_line;
  const std::size_t split_at = second_start + 2;
  CHECK(novel::split_chapter_numbered(parsed.chapters, 2, split_at, "", "插曲",
                                      2));
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(7));
  if (parsed.chapters.size() != 7) return;

  // 当前章保持自己的序号（中文数字样式不变）。
  CHECK_EQ(parsed.chapters[2].title, std::string("第二章 相遇"));
  CHECK_EQ(parsed.chapters[2].number, std::string("第二章"));
  CHECK_EQ(parsed.chapters[2].end_line, split_at);

  // 新章插入其后，序号+1、名称由参数给出。
  const novel::Chapter& fresh = parsed.chapters[3];
  CHECK(fresh.inserted);
  CHECK(fresh.renamed);
  CHECK(!fresh.excluded_from_numbering);
  CHECK_EQ(fresh.number, std::string("第三章"));
  CHECK_EQ(fresh.name, std::string("插曲"));
  CHECK_EQ(fresh.title, std::string("第三章 插曲"));
  CHECK_EQ(fresh.start_line, split_at);
  CHECK_EQ(fresh.end_line, second_end);
  CHECK_EQ(fresh.index, 3);
  CHECK(fresh.numbered);

  // 后续参与编号的章节各 +1，name 不变。
  CHECK_EQ(parsed.chapters[4].number, std::string("第四章"));
  CHECK_EQ(parsed.chapters[4].name, std::string("离别"));
  CHECK_EQ(parsed.chapters[4].title, std::string("第四章 离别"));
  CHECK_EQ(parsed.chapters[5].title, std::string("终章"));
  CHECK_EQ(parsed.chapters[6].title, std::string("后记"));
  CHECK_EQ(parsed.chapters[0].title, std::string("序章"));

  // 排除章不占号：在其前拆章时后续编号不动。
  novel::set_excluded_from_numbering(parsed.chapters[4], true);
  CHECK(novel::split_chapter_numbered(parsed.chapters, 3,
                                      parsed.chapters[3].start_line + 1, "",
                                      "再次插入", 2));
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(8));
  if (parsed.chapters.size() == 8) {
    CHECK_EQ(parsed.chapters[4].number, std::string("第四章"));
    CHECK_EQ(parsed.chapters[5].title, std::string("第四章 离别"));
    CHECK(parsed.chapters[5].excluded_from_numbering);
  }

  const std::string rendered =
      novel::render_txt(loaded.text, parsed.chapters, parsed.toc_lines);
  const auto after = non_empty_lines(rendered);
  CHECK(line_index(after, "第三章 插曲") >= 0);
  CHECK(line_index(after, "第四章 离别") >= 0);
}

void test_preface_split_optional_numbering() {
  g_test = "preface_split_optional_numbering";
  const auto make_chapters = []() {
    std::vector<novel::Chapter> chs;
    novel::Chapter preface;
    preface.title = "（前言）";
    preface.name = "（前言）";
    preface.level = 2;
    preface.start_line = 0;
    preface.end_line = 2;
    preface.synthetic = true;
    chs.push_back(preface);

    novel::Chapter one;
    one.title = "第1章 一";
    one.number = "第1章";
    one.name = "一";
    one.numbered = true;
    one.level = 2;
    one.start_line = 2;
    one.end_line = 4;
    chs.push_back(one);

    novel::Chapter two;
    two.title = "第2章 二";
    two.number = "第2章";
    two.name = "二";
    two.numbered = true;
    two.level = 2;
    two.start_line = 4;
    two.end_line = 6;
    chs.push_back(two);
    return chs;
  };

  // 不勾选参与：新章无编号，后续章节序号不能被凭空改动。
  auto plain = make_chapters();
  CHECK(novel::split_chapter_numbered(plain, 0, 1, "", "开场", 2));
  CHECK_EQ(plain.size(), static_cast<std::size_t>(4));
  if (plain.size() == 4) {
    CHECK(!plain[1].numbered);
    CHECK_EQ(plain[1].title, std::string("开场"));
    CHECK_EQ(plain[2].number, std::string("第1章"));
    CHECK_EQ(plain[3].number, std::string("第2章"));
  }

  // 勾选参与并填写序号：新章占号，后续参与章节依次 +1。
  auto numbered = make_chapters();
  CHECK(novel::split_chapter_numbered(numbered, 0, 1, "第1章", "开场", 2));
  CHECK_EQ(numbered.size(), static_cast<std::size_t>(4));
  if (numbered.size() == 4) {
    CHECK(numbered[1].numbered);
    CHECK_EQ(numbered[1].number, std::string("第1章"));
    CHECK_EQ(numbered[1].name, std::string("开场"));
    CHECK_EQ(numbered[2].number, std::string("第2章"));
    CHECK_EQ(numbered[3].number, std::string("第3章"));
  }
}

void test_promote_unnumbered_chapter() {
  g_test = "promote_unnumbered_chapter";
  auto make_chapters = []() {
    std::vector<novel::Chapter> chs;
    novel::Chapter preface;
    preface.title = "序章";
    preface.name = "序章";
    preface.level = 2;
    preface.start_line = 0;
    preface.end_line = 2;
    chs.push_back(preface);

    novel::Chapter one;
    one.title = "第1章 启程";
    one.number = "第1章";
    one.name = "启程";
    one.separator = " ";
    one.numbered = true;
    one.level = 2;
    one.start_line = 2;
    one.end_line = 4;
    chs.push_back(one);

    novel::Chapter two;
    two.title = "第2章 相遇";
    two.number = "第2章";
    two.name = "相遇";
    two.separator = " ";
    two.numbered = true;
    two.level = 2;
    two.start_line = 4;
    two.end_line = 6;
    chs.push_back(two);

    novel::Chapter tail;
    tail.title = "后记";
    tail.name = "后记";
    tail.level = 2;
    tail.start_line = 6;
    tail.end_line = 8;
    chs.push_back(tail);
    return chs;
  };

  auto chs = make_chapters();
  CHECK(novel::promote_to_numbering(chs, 0));
  CHECK(chs[0].numbered);
  CHECK_EQ(chs[0].number, std::string("第1章"));
  CHECK_EQ(chs[0].name, std::string("序章"));
  CHECK_EQ(chs[0].title, std::string("第1章 序章"));
  CHECK_EQ(chs[1].number, std::string("第2章"));
  CHECK_EQ(chs[2].number, std::string("第3章"));
  CHECK(!chs[3].numbered);

  // 最后一章按前一章样式 +1，不再顺延。
  CHECK(novel::promote_to_numbering(chs, 3));
  CHECK(chs[3].numbered);
  CHECK_EQ(chs[3].number, std::string("第4章"));
  CHECK_EQ(chs[3].title, std::string("第4章 后记"));

  // 全书无编号章时默认第1章。
  auto only = std::vector<novel::Chapter>{make_chapters().front()};
  CHECK(novel::promote_to_numbering(only, 0));
  CHECK_EQ(only[0].number, std::string("第1章"));

  // 已有编号章不能重复加入。
  CHECK(!novel::promote_to_numbering(chs, 0));
}

void test_columns_no_space_render() {
  g_test = "columns_no_space_render";
  const std::string text = "第1章鞋跟先生！\n\n正文一。\n";
  const auto parsed = novel::parse_chapters(text);
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(1));
  if (parsed.chapters.empty()) return;
  CHECK_EQ(parsed.chapters[0].number, std::string("第1章"));
  CHECK_EQ(parsed.chapters[0].separator, std::string(""));
  CHECK_EQ(parsed.chapters[0].name, std::string("鞋跟先生！"));
  CHECK_EQ(parsed.chapters[0].title, std::string("第1章鞋跟先生！"));
  CHECK(parsed.chapters[0].numbered);

  // 拆分不改原文，默认渲染逐字节一致（不额外补空格）。
  CHECK_EQ(novel::render_txt(text, parsed.chapters, parsed.toc_lines), text);

  // 补编号时仍按分隔符为空拼接，不会插入空格。
  auto copied = parsed;
  CHECK(novel::renumber_chapters(copied.chapters, 1, false));
  CHECK_EQ(copied.chapters[0].title, std::string("第1章鞋跟先生！"));
  CHECK_EQ(novel::render_txt(text, copied.chapters, copied.toc_lines), text);
}

void test_inserted_rename_render() {
  g_test = "inserted_rename_render";
  const std::string text = "第1章 甲\n\n甲一。\n\n甲二。\n";
  auto parsed = novel::parse_chapters(text);
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(1));
  if (parsed.chapters.size() != 1) return;

  CHECK(novel::split_chapter(parsed.chapters, 0, 2, "插曲", 2));
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(2));
  if (parsed.chapters.size() != 2) return;
  CHECK(novel::rename_chapter(parsed.chapters[1], "新标题"));

  const std::string rendered =
      novel::render_txt(text, parsed.chapters, parsed.toc_lines);
  CHECK(rendered.find("新标题") != std::string::npos);
  CHECK(rendered.find("插曲") == std::string::npos);
}

void test_preface_move_body_next() {
  g_test = "preface_move_body_next";
  const std::string text = "前言一。\n\n前言二。\n\n第1章 甲\n\n甲一。\n";
  auto parsed = novel::parse_chapters(text);
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(2));
  if (parsed.chapters.size() != 2) return;
  CHECK(parsed.chapters[0].synthetic);
  CHECK_EQ(parsed.chapters[0].start_line, static_cast<std::size_t>(0));
  const std::size_t preface_end = parsed.chapters[0].end_line;
  CHECK(preface_end > 0);

  CHECK(novel::move_body_to_next(parsed.chapters, 0));
  CHECK_EQ(parsed.chapters[0].end_line, static_cast<std::size_t>(0));
  CHECK_EQ(parsed.chapters[1].extra_body.size(), static_cast<std::size_t>(1));
  if (!parsed.chapters[1].extra_body.empty()) {
    CHECK_EQ(parsed.chapters[1].extra_body.front().begin,
             static_cast<std::size_t>(0));
    CHECK_EQ(parsed.chapters[1].extra_body.front().end, preface_end);
  }

  const std::string rendered =
      novel::render_txt(text, parsed.chapters, parsed.toc_lines);
  const auto after = non_empty_lines(rendered);
  CHECK(line_index(after, "前言一。") >= 0);
  CHECK(line_index(after, "前言二。") >= 0);
  CHECK(line_index(after, "第1章 甲") >= 0);
  CHECK_EQ(sorted_lines(after), sorted_lines(non_empty_lines(text)));
}

void test_columns_move_body_previous() {
  g_test = "columns_move_body_previous";
  const auto loaded = load("stage2/09_综合与回归/09-01_完整小说_标准结构.txt");
  CHECK(loaded.ok);
  if (!loaded.ok) return;
  auto parsed = novel::parse_chapters(loaded.text);
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(6));
  if (parsed.chapters.size() != 6) return;

  CHECK(!novel::move_body_to_previous(parsed.chapters, 0));
  CHECK(!novel::move_body_to_previous(parsed.chapters, 6));

  const std::size_t target_end = parsed.chapters[2].end_line;
  const std::size_t target_start = parsed.chapters[2].start_line;
  const std::size_t previous_end = parsed.chapters[1].end_line;
  CHECK(target_end > previous_end);
  CHECK(novel::move_body_to_previous(parsed.chapters, 2));
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(6));
  CHECK_EQ(parsed.chapters[1].end_line, target_end);
  CHECK_EQ(parsed.chapters[2].end_line, target_start + 1);
  CHECK(parsed.chapters[2].extra_body.empty());
  CHECK(std::binary_search(parsed.chapters[1].skip_lines.begin(),
                           parsed.chapters[1].skip_lines.end(), target_start));

  const std::string rendered =
      novel::render_txt(loaded.text, parsed.chapters, parsed.toc_lines);
  const auto after = non_empty_lines(rendered);
  // 当前章只剩标题；正文已并进前一章，出现在其标题之前的位置。
  const int title = line_index(after, "第二章 相遇");
  const int body = line_index(after, "他们在桥上相遇。");
  const int before = line_index(after, "少年背起行囊。");
  CHECK(title >= 0);
  CHECK(body >= 0);
  CHECK(before >= 0);
  if (body >= 0 && before >= 0 && title >= 0) {
    CHECK(before < body);
    CHECK(body < title);
  }
  CHECK_EQ(sorted_lines(after), sorted_lines(non_empty_lines(loaded.text)));
  CHECK_EQ(after.size(), non_empty_lines(loaded.text).size());
}

void test_columns_move_body_next() {
  g_test = "columns_move_body_next";
  const auto loaded = load("stage2/09_综合与回归/09-01_完整小说_标准结构.txt");
  CHECK(loaded.ok);
  if (!loaded.ok) return;
  auto parsed = novel::parse_chapters(loaded.text);
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(6));
  if (parsed.chapters.size() != 6) return;

  CHECK(!novel::move_body_to_next(parsed.chapters, 5));
  CHECK(!novel::move_body_to_next(parsed.chapters, -1));

  const std::size_t moved_begin = parsed.chapters[1].start_line + 1;
  const std::size_t moved_end = parsed.chapters[1].end_line;
  CHECK(novel::move_body_to_next(parsed.chapters, 1));
  CHECK_EQ(parsed.chapters[1].end_line, moved_begin);
  CHECK(parsed.chapters[1].extra_body.empty());
  CHECK_EQ(parsed.chapters[2].extra_body.size(), static_cast<std::size_t>(1));
  if (!parsed.chapters[2].extra_body.empty()) {
    CHECK_EQ(parsed.chapters[2].extra_body.front().begin, moved_begin);
    CHECK_EQ(parsed.chapters[2].extra_body.front().end, moved_end);
  }

  const std::string rendered =
      novel::render_txt(loaded.text, parsed.chapters, parsed.toc_lines);
  const auto after = non_empty_lines(rendered);
  const int first_title = line_index(after, "第一章 启程");
  const int next_title = line_index(after, "第二章 相遇");
  const int moved = line_index(after, "少年背起行囊。");
  const int next_body = line_index(after, "他们在桥上相遇。");
  CHECK(first_title >= 0);
  CHECK(next_title >= 0);
  CHECK(moved >= 0);
  CHECK(next_body >= 0);
  if (first_title >= 0 && next_title >= 0 && moved >= 0 && next_body >= 0) {
    CHECK(first_title < next_title);
    CHECK(next_title < moved);  // 下一章标题先出
    CHECK(moved < next_body);   // 再当前章正文，再下一章正文
  }
  CHECK_EQ(sorted_lines(after), sorted_lines(non_empty_lines(loaded.text)));
}

void test_columns_extra_body() {
  g_test = "columns_extra_body";
  const std::string text =
      "第1章 甲\n\n甲一。\n\n甲二。\n\n第2章 乙\n\n乙一。\n";
  auto parsed = novel::parse_chapters(text);
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(2));
  if (parsed.chapters.size() != 2) return;

  const std::vector<std::string> lines = novel::split_lines(text);
  const std::size_t second_title = parsed.chapters[1].start_line;
  CHECK_EQ(second_title, static_cast<std::size_t>(6));

  // 把第二章正文划给第一章：extra_body 排在主正文之后、下一章标题之前。
  parsed.chapters[0].extra_body.push_back({second_title + 1, lines.size()});
  parsed.chapters[1].end_line = second_title + 1;

  const std::string rendered =
      novel::render_txt(text, parsed.chapters, parsed.toc_lines);
  CHECK_EQ(non_empty_lines(rendered),
           (std::vector<std::string>{"第1章 甲", "乙一。", "甲一。", "甲二。",
                                     "第2章 乙"}));

  // 空 extra_body 走原路径，输出不变。
  auto plain = novel::parse_chapters(text);
  CHECK_EQ(novel::render_txt(text, plain.chapters, plain.toc_lines), text);
}

void test_columns_extra_body_order() {
  g_test = "columns_extra_body_order";
  const std::string text =
      "第1章 甲\n\n甲一。\n\n第2章 乙\n\n乙一。\n\n第3章 丙\n\n丙一。\n";
  auto parsed = novel::parse_chapters(text);
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(3));
  if (parsed.chapters.size() != 3) return;

  // 先把甲划给乙，再把乙划给丙：丙里的 extra_body 必须仍按源行顺序，
  // 不能因为后一次插入而倒序。
  CHECK(novel::move_body_to_next(parsed.chapters, 0));
  CHECK(novel::move_body_to_next(parsed.chapters, 1));
  CHECK(parsed.chapters[0].extra_body.empty());
  CHECK(parsed.chapters[1].extra_body.empty());
  CHECK_EQ(parsed.chapters[2].extra_body.size(), static_cast<std::size_t>(2));
  if (parsed.chapters[2].extra_body.size() == 2) {
    CHECK(parsed.chapters[2].extra_body[0].begin <
          parsed.chapters[2].extra_body[1].begin);
  }

  const std::string rendered =
      novel::render_txt(text, parsed.chapters, parsed.toc_lines);
  const auto after = non_empty_lines(rendered);
  const int first = line_index(after, "甲一。");
  const int second = line_index(after, "乙一。");
  const int third = line_index(after, "丙一。");
  CHECK(first >= 0);
  CHECK(second >= 0);
  CHECK(third >= 0);
  if (first >= 0 && second >= 0 && third >= 0) {
    CHECK(first < second);
    CHECK(second < third);
  }
  CHECK_EQ(sorted_lines(after), sorted_lines(non_empty_lines(text)));
}

void test_columns_extra_body_stack() {
  g_test = "columns_extra_body_stack";
  const auto loaded = load("stage2/09_综合与回归/09-01_完整小说_标准结构.txt");
  CHECK(loaded.ok);
  if (!loaded.ok) return;
  auto parsed = novel::parse_chapters(loaded.text);
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(6));
  if (parsed.chapters.size() != 6) return;

  // 改名 + 不参与编号 + 拆章 + 章节正文划转 叠加。
  CHECK(novel::rename_chapter(parsed.chapters[1], "第一章 改名"));
  novel::set_excluded_from_numbering(parsed.chapters[2], true);
  const std::size_t third_start = parsed.chapters[3].start_line;
  CHECK(novel::split_chapter_numbered(parsed.chapters, 3, third_start + 2, "",
                                      "插曲", 2));
  CHECK_EQ(parsed.chapters.size(), static_cast<std::size_t>(7));
  if (parsed.chapters.size() != 7) return;
  CHECK_EQ(parsed.chapters[4].title, std::string("第四章 插曲"));
  CHECK(novel::move_body_to_next(parsed.chapters, 4));
  CHECK(!parsed.chapters[5].extra_body.empty());

  const std::string rendered =
      novel::render_txt(loaded.text, parsed.chapters, parsed.toc_lines);
  const auto after = non_empty_lines(rendered);
  CHECK(line_index(after, "第一章 改名") >= 0);
  CHECK(line_index(after, "第一章 启程") < 0);
  CHECK(line_index(after, "第二章 相遇") >= 0);  // 仅不参与编号，仍渲染
  const int inserted = line_index(after, "第四章 插曲");
  const int moved = line_index(after, "终究还是要走。");
  const int ending_title = line_index(after, "终章");
  const int ending_body = line_index(after, "故事落幕。");
  CHECK(inserted >= 0);
  CHECK(moved >= 0);
  CHECK(ending_title >= 0);
  CHECK(ending_body >= 0);
  if (inserted >= 0 && moved >= 0 && ending_title >= 0 && ending_body >= 0) {
    CHECK(inserted < moved);
    CHECK(ending_title < moved);
    CHECK(moved < ending_body);
  }
  CHECK_EQ(after.size(), non_empty_lines(loaded.text).size() + 1);
  auto expected = non_empty_lines(loaded.text);
  expected.erase(std::remove(expected.begin(), expected.end(),
                             std::string("第一章 启程")),
                 expected.end());
  expected.push_back("第一章 改名");
  expected.push_back("第四章 插曲");
  CHECK_EQ(sorted_lines(after), sorted_lines(expected));
}

// 差分校验：对同一标题，split_heading_display 必须复现解析时的两列结果。
// 解析语法与显示拆分目前各有实现，这条用例用来锁住它们不再各自漂移。
void test_display_matches_parse() {
  g_test = "display_matches_parse";
  std::size_t files = 0;
  const auto check_file = [&](const std::filesystem::path& path) {
    const auto loaded = novel::load_text_file(path);
    if (!loaded.ok) {
      CHECK(loaded.ok);
      return;
    }
    const auto parsed = novel::parse_chapters(loaded.text);
    ++files;
    for (const auto& chapter : parsed.chapters) {
      novel::Chapter probe;
      novel::split_heading_display(chapter.title, probe);
      CHECK_EQ(probe.number, chapter.number);
      CHECK_EQ(probe.name, chapter.name);
      CHECK_EQ(probe.separator, chapter.separator);
      CHECK_EQ(probe.numbered, chapter.numbered);
    }
  };

  for (const auto& entry : std::filesystem::recursive_directory_iterator(
           data_path("stage2"))) {
    if (!entry.is_regular_file()) continue;
    if (entry.path().extension() != ".txt") continue;
    check_file(entry.path());
  }
  for (const auto& entry :
       std::filesystem::directory_iterator(data_path("real"))) {
    if (!entry.is_regular_file()) continue;
    if (entry.path().extension() != ".txt") continue;
    check_file(entry.path());
  }
  for (const auto& entry :
       std::filesystem::directory_iterator(data_path(""))) {
    if (!entry.is_regular_file()) continue;
    if (entry.path().extension() != ".txt") continue;
    check_file(entry.path());
  }
  CHECK_EQ(files, static_cast<std::size_t>(133));
}

void test_columns_title_invariant() {
  g_test = "columns_title_invariant";
  std::size_t files = 0;
  const auto check_file = [&](const std::filesystem::path& path) {
    const auto loaded = novel::load_text_file(path);
    if (!loaded.ok) {
      CHECK(loaded.ok);
      return;
    }
    const auto parsed = novel::parse_chapters(loaded.text);
    ++files;
    for (const auto& chapter : parsed.chapters) {
      if (!chapter.number.empty()) {
        CHECK_EQ(chapter.number + chapter.separator + chapter.name,
                 chapter.title);
      } else {
        CHECK_EQ(chapter.separator, std::string(""));
        CHECK_EQ(chapter.name, chapter.title);
        CHECK(!chapter.numbered);
      }
    }
  };

  // 两列拼回完整标题：全部 130 个阶段 2 样本 + 2 个大型合成样本 + 根目录样本。
  for (const auto& entry : std::filesystem::recursive_directory_iterator(
           data_path("stage2"))) {
    if (!entry.is_regular_file()) continue;
    if (entry.path().extension() != ".txt") continue;
    check_file(entry.path());
  }
  for (const auto& entry :
       std::filesystem::directory_iterator(data_path("real"))) {
    if (!entry.is_regular_file()) continue;
    if (entry.path().extension() != ".txt") continue;
    check_file(entry.path());
  }
  for (const auto& entry :
       std::filesystem::directory_iterator(data_path(""))) {
    if (!entry.is_regular_file()) continue;
    if (entry.path().extension() != ".txt") continue;
    check_file(entry.path());
  }
  CHECK_EQ(files, static_cast<std::size_t>(133));
}

struct TestCase {
  const char* name;
  void (*run)();
};
}  // namespace

int main() {
  const TestCase tests[] = {
      {"standard_novel", test_standard_novel},
      {"special_preface", test_special_preface},
      {"false_positive_traps", test_false_positive_traps},
      {"toc_region", test_toc_region},
      {"toc_duplicate_titles_hash", test_toc_duplicate_titles_hash},
      {"toc_duplicate_titles_hash_large",
       test_toc_duplicate_titles_hash_large},
      {"bracket_numbering", test_bracket_numbering},
      {"volume_levels", test_volume_levels},
      {"indented_title", test_indented_title},
      {"plain_novel_sample", test_plain_novel_sample},
      {"large_book_gb18030", test_large_book_gb18030},
      {"large_book_bom", test_large_book_bom},
      {"reorder_and_render", test_reorder_and_render},
      {"encoding_roundtrip", test_encoding_roundtrip},
      {"encoding_lossy_fallback", test_encoding_lossy_fallback},
      {"empty_and_headerless", test_empty_and_headerless},
      {"title_lines_option", test_title_lines_option},
      {"hash_prefix_without_space", test_hash_prefix_without_space},
      {"markdown_levels", test_markdown_levels},
      {"spaced_numbering", test_spaced_numbering},
      {"bracketed_numbering", test_bracketed_numbering},
      {"large_chinese_numerals", test_large_chinese_numerals},
      {"prose_after_separator", test_prose_after_separator},
      {"title_punctuation_regression", test_title_punctuation_regression},
      {"false_positive_suite", test_false_positive_suite},
      {"list_numbering_not_chapters", test_list_numbering_not_chapters},
      {"title_lines_rejects_english_prose",
       test_title_lines_rejects_english_prose},
      {"edit_rename", test_edit_rename},
      {"edit_level_markdown", test_edit_level_markdown},
      {"edit_merge_keep_heading", test_edit_merge_keep_heading},
      {"edit_merge_suppress", test_edit_merge_suppress},
      {"edit_merge_chain_suppress", test_edit_merge_chain_suppress},
      {"edit_delete", test_edit_delete},
      {"edit_split", test_edit_split},
      {"edit_append", test_edit_append},
      {"edit_renumber_arabic", test_edit_renumber_arabic},
      {"edit_renumber_chinese", test_edit_renumber_chinese},
      {"edit_delete_reorder_render", test_edit_delete_reorder_render},
      {"invalid_content_partial_line", test_invalid_content_partial_line},
      {"invalid_content_multi_line", test_invalid_content_multi_line},
      {"invalid_content_whole_line", test_invalid_content_whole_line},
      {"invalid_content_overlap_merge", test_invalid_content_overlap_merge},
      {"invalid_content_utf8_boundary", test_invalid_content_utf8_boundary},
      {"invalid_content_with_edits", test_invalid_content_with_edits},
      {"columns_parse_split", test_columns_parse_split},
      {"columns_false_positive_06", test_columns_false_positive_06},
      {"columns_rename_parts", test_columns_rename_parts},
      {"columns_renumber_pad", test_columns_renumber_pad},
      {"columns_split_numbered", test_columns_split_numbered},
      {"preface_split_optional_numbering", test_preface_split_optional_numbering},
      {"columns_no_space_render", test_columns_no_space_render},
      {"promote_unnumbered_chapter", test_promote_unnumbered_chapter},
      {"inserted_rename_render", test_inserted_rename_render},
      {"preface_move_body_next", test_preface_move_body_next},
      {"columns_move_body_previous", test_columns_move_body_previous},
      {"columns_move_body_next", test_columns_move_body_next},
      {"columns_extra_body", test_columns_extra_body},
      {"columns_extra_body_order", test_columns_extra_body_order},
      {"columns_extra_body_stack", test_columns_extra_body_stack},
      {"display_matches_parse", test_display_matches_parse},
      {"columns_title_invariant", test_columns_title_invariant},
  };

  for (const TestCase& test : tests) {
    const int before = g_failures;
    g_test = test.name;
    std::printf("[ RUN  ] %s\n", test.name);
    test.run();
    std::printf("[ %s ] %s\n", before == g_failures ? " OK " : "FAIL", test.name);
  }

  std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
  return g_failures == 0 ? 0 : 1;
}
