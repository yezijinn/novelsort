#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace novel {

// 源行半开区间 [begin, end)，0 基。
struct LineRange {
  std::size_t begin = 0;
  std::size_t end = 0;
};

// 源文本中的一段无效内容；行号 0 基，字节偏移为 UTF-8 半开区间 [begin_byte,end_byte)。
struct TextExclusion {
  std::size_t line = 0;
  std::size_t begin_byte = 0;
  std::size_t end_byte = 0;
};

struct Chapter {
  int index = 0;              // 0 基，当前顺序
  int level = 2;              // 1=卷/部/篇, 2=章/节/话/特殊章
  std::string title;          // 完整标题文本（不含 # 装饰）
  std::string heading_line;   // 原始标题行（不含换行）；合成章为空
  std::size_t start_line = 0; // 标题行 0 基行号；合成章为 0
  std::size_t end_line = 0;   // 独占下界
  bool synthetic = false;

  // ---- 章节目录编辑状态（默认值保持旧的只读行为） ----
  bool renamed = false;      // title 是用户改过的
  bool level_changed = false;  // level 是用户改过的（Markdown 井号按新层级重写）
  bool inserted = false;     // 标题是拆分/新增插入的，start_line 是正文首行，标题不在原文
  bool suppressed = false;   // 导出时丢弃原标题行
  // 导出时跳过的源行号（合并丢标题等）。必须升序无重复：render_txt 用
  // binary_search 查找，写入路径统一由 DirectoryEdit 的 dedupe_skip_lines 维护。
  std::vector<std::size_t> skip_lines;

  // ---- 目录两列显示：number=序号列，name=标题列 ----
  // title 仍是完整标题（number+separator+name，或 name），JSON 不输出这些字段。
  std::string number;     // 例如「第1章」「第001章」「序章」「Chapter 1」「1.」
  std::string name;       // 例如「鞋跟先生！」
  std::string separator;  // number 与 name 之间的原分隔符（" " / "　" / "：" / ""）
  bool numbered = false;               // number 是否是可递增的序号
  bool excluded_from_numbering = false;  // 用户标记「不参与排序」（仍可移动）
  // 划给本章、但不属于 [start_line+1, end_line) 的正文行；渲染时排在主正文之前。
  std::vector<LineRange> extra_body;
};

// 按当前顺序重写 index 字段。移动 / 插入 / 删除后必须调用，否则 index 与下标脱节。
void reindex_chapters(std::vector<Chapter>& chapters);

}  // namespace novel
