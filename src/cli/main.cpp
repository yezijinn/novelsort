#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "core/ChapterOrder.hpp"
#include "core/ChapterParser.hpp"
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

constexpr int kExitOk = 0;
constexpr int kExitFile = 2;
constexpr int kExitArgs = 3;

void print_usage() {
  std::fprintf(stderr,
               "用法：novellines-cli --input <file> [--out <file>] "
               "[--json <file>] [--move from:to ...]\n"
               "                 [--title-lines] [--dry-run]\n"
               "无参数或参数错误时打印本帮助并返回 %d。\n",
               kExitArgs);
}

struct Options {
  std::string input;
  std::string json_path;
  std::string out_path;
  std::vector<std::pair<int, int>> moves;
  bool title_lines = false;
  bool dry_run = false;
};

void print_error(const std::string& message) {
  std::fputs(message.c_str(), stderr);
  std::fputc('\n', stderr);
}

std::filesystem::path path_from_utf8(const std::string& text) {
  return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

bool parse_move(const std::string& text, std::pair<int, int>& move) {
  const std::size_t colon = text.find(':');
  if (colon == std::string::npos) return false;
  const std::string from = text.substr(0, colon);
  const std::string to = text.substr(colon + 1);
  if (from.empty() || to.empty()) return false;
  try {
    std::size_t used_from = 0;
    std::size_t used_to = 0;
    const int from_value = std::stoi(from, &used_from);
    const int to_value = std::stoi(to, &used_to);
    if (used_from != from.size() || used_to != to.size()) return false;
    move = {from_value, to_value};
    return true;
  } catch (...) {
    return false;
  }
}

bool write_text_file(const std::string& path, const std::string& content) {
  std::ofstream out(path_from_utf8(path), std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(content.data(), static_cast<std::streamsize>(content.size()));
  out.flush();
  return static_cast<bool>(out);
}

bool parse_args(const std::vector<std::string>& args, Options& options,
                std::string& error) {
  for (std::size_t i = 1; i < args.size(); ++i) {
    const std::string& arg = args[i];
    auto need_value = [&](std::string& target) {
      if (i + 1 >= args.size()) {
        error = "参数缺少取值：" + arg;
        return false;
      }
      target = args[++i];
      return true;
    };
    if (arg == "--input") {
      if (!need_value(options.input)) return false;
    } else if (arg == "--json") {
      if (!need_value(options.json_path)) return false;
    } else if (arg == "--out") {
      if (!need_value(options.out_path)) return false;
    } else if (arg == "--move") {
      std::string value;
      if (!need_value(value)) return false;
      std::pair<int, int> move{};
      if (!parse_move(value, move)) {
        error = "无法解析 --move：" + value + "（应为 from:to）";
        return false;
      }
      options.moves.push_back(move);
    } else if (arg == "--title-lines") {
      options.title_lines = true;
    } else if (arg == "--dry-run") {
      options.dry_run = true;
    } else {
      error = "未知参数：" + arg;
      return false;
    }
  }
  if (options.input.empty()) {
    error = "缺少 --input";
    return false;
  }
  return true;
}

int run(const std::vector<std::string>& args) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
#endif
  if (args.size() <= 1) {
    print_usage();
    return kExitArgs;
  }

  Options options;
  std::string arg_error;
  if (!parse_args(args, options, arg_error)) {
    print_error(arg_error);
    return kExitArgs;
  }

  const novel::TextLoadResult loaded =
      novel::load_text_file(path_from_utf8(options.input));
  if (!loaded.ok) {
    print_error(loaded.error);
    return kExitFile;
  }

  novel::ParseOptions parse_options;
  parse_options.accept_unnumbered_titles = options.title_lines;
  const novel::ParseResult parsed = novel::parse_chapters(loaded.text, parse_options);

  std::fprintf(stderr, "编码: %s\n", loaded.encoding.c_str());
  for (const auto& warning : loaded.warnings) {
    std::fprintf(stderr, "提示: %s\n", warning.c_str());
  }

  for (const novel::Chapter& chapter : parsed.chapters) {
    if (chapter.synthetic) {
      std::fprintf(stderr, "合成章: %s（第 %zu-%zu 行）\n", chapter.title.c_str(),
                   chapter.start_line, chapter.end_line);
    }
  }
  std::fprintf(stderr, "目录区行数: %zu\n", parsed.toc_lines.size());
  for (const auto& warning : parsed.warnings) {
    std::fprintf(stderr, "警告: %s\n", warning.c_str());
  }

  const int chapter_count = static_cast<int>(parsed.chapters.size());
  std::vector<std::pair<int, int>> moves = options.moves;
  for (const auto& move : moves) {
    if (move.first < 1 || move.first > chapter_count) {
      std::fprintf(stderr, "move 越界：%d（有效范围 1-%d）\n", move.first,
                   chapter_count);
      return kExitArgs;
    }
    if (move.second < 1 || move.second > chapter_count) {
      std::fprintf(stderr, "提示: move 目标 %d 越界，已钳制到 1-%d\n",
                   move.second, chapter_count);
    }
  }

  const std::vector<novel::Chapter> ordered =
      novel::reorder_chapters(parsed.chapters, moves);

  if (options.dry_run) {
    std::printf("章节数: %d\n", chapter_count);
    std::printf("总行数: %zu\n",
                novel::split_lines_view(loaded.text).size());
    std::printf("目录区行数: %zu\n", parsed.toc_lines.size());
    std::printf("重排后顺序: ");
    for (std::size_t i = 0; i < ordered.size(); ++i) {
      std::printf("%s%s", i == 0 ? "" : " > ", ordered[i].title.c_str());
    }
    std::printf("\n");
    return kExitOk;
  }

  for (std::size_t i = 0; i < ordered.size(); ++i) {
    std::printf("%zu\t%s\t%s\n", i + 1,
                ordered[i].level == 1 ? "VOL" : "CH",
                ordered[i].title.c_str());
  }

  if (!options.json_path.empty()) {
    std::vector<std::string> all_warnings = loaded.warnings;
    all_warnings.insert(all_warnings.end(), parsed.warnings.begin(),
                        parsed.warnings.end());
    const std::string json =
        novel::directory_json(options.input, loaded.encoding, ordered,
                              parsed.toc_lines, all_warnings);
    if (!write_text_file(options.json_path, json)) {
      print_error("无法写入 JSON：" + options.json_path);
      return kExitFile;
    }
    std::fprintf(stderr, "已写入 JSON: %s\n", options.json_path.c_str());
  }

  if (!options.out_path.empty()) {
    const std::string rendered = novel::render_txt(
        loaded.text, ordered, parsed.toc_lines);
    if (!write_text_file(options.out_path, rendered)) {
      print_error("无法写入 TXT：" + options.out_path);
      return kExitFile;
    }
    std::fprintf(stderr, "已写入 TXT: %s\n", options.out_path.c_str());
  }

  return kExitOk;
}

}  // namespace

#ifdef _WIN32

std::string wide_to_utf8(const wchar_t* text) {
  const int len = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr,
                                      nullptr);
  if (len <= 0) return {};
  std::string out(static_cast<std::size_t>(len), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), len, nullptr, nullptr);
  out.resize(static_cast<std::size_t>(len - 1));
  return out;
}

int wmain(int argc, wchar_t** argv) {
  std::vector<std::string> args;
  args.reserve(static_cast<std::size_t>(argc));
  for (int i = 0; i < argc; ++i) args.push_back(wide_to_utf8(argv[i]));
  return run(args);
}

#else

int main(int argc, char** argv) {
  std::vector<std::string> args(argv, argv + argc);
  return run(args);
}

#endif
