#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "core/Chapter.hpp"

namespace novel {

enum class ScriptSegmentType { heading, narration, dialogue, thought };

// 有声书脚本里的角色表。第一版只有旁白。
struct ScriptCharacter {
  std::string id;
  std::string name;
  std::string gender;
  bool is_narrator = false;
  std::vector<std::string> aliases;
};

// 与输入章节一一对应；index 从 1 开始。合成章没有源行，source_start/end 为 0。
struct ScriptChapter {
  int index = 0;
  std::string title;
  int level = 2;
  bool synthetic = false;  // 合成章或插入章：源文中没有对应标题行
  std::size_t start_line = 0;
  std::size_t end_line = 0;
  std::size_t source_start = 0;
  std::size_t source_end = 0;
};

// text 是源文本的精确子串；source_start/end 是解码、统一换行后的 UTF-8
// 字节偏移，半开区间。
struct ScriptSegment {
  int id = 0;
  int chapter_index = 0;
  ScriptSegmentType type = ScriptSegmentType::narration;
  std::string speaker_id;    // narration/heading 固定 narrator；对话/心声暂空
  std::string speaker_hint;  // 归属语候选，可为空
  std::string text;
  std::string speak_text;  // 去外层引号、换行改空格、压缩空白
  std::size_t source_start = 0;
  std::size_t source_end = 0;
  std::string emotion;
  std::string tone;
  float confidence = 1.0f;
  bool needs_review = false;
};

struct ScriptDocument {
  std::string source;
  std::string encoding;
  std::vector<ScriptCharacter> characters;
  std::vector<ScriptChapter> chapters;
  std::vector<ScriptSegment> segments;
};

struct ScriptBuildOptions {
  bool include_headings = true;
};

ScriptDocument build_script(std::string_view text,
                            const std::vector<Chapter>& chapters,
                            const std::vector<std::size_t>& toc_lines,
                            std::string source, std::string encoding,
                            const ScriptBuildOptions& options = {});

// 单行 UTF-8 JSON。
std::string script_json(const ScriptDocument& document);

// 返回错误描述；空表示通过。source_text 是 build_script 的同一个源文本。
std::vector<std::string> validate_script(const ScriptDocument& document,
                                         std::string_view source_text);

const char* script_type_name(ScriptSegmentType type);

}  // namespace novel
