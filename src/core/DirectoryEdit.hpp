#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "core/Chapter.hpp"

namespace novel {

// 目录编辑：纯函数，全部做边界检查。
// 返回 false 表示参数不合法；失败时输入保持不变，不抛异常。
// LineRange 定义在 core/Chapter.hpp（渲染器与本模块共用）。

// 改标题（trim 后为空则失败）。合成章也可改名，只影响目录 JSON 与预览标题。
bool rename_chapter(Chapter& ch, std::string_view title);

// 改层级，只接受 1（卷/部/篇）与 2（章/节/话）。
bool set_chapter_level(Chapter& ch, int level);

// 把第 index 章并入前一章。suppress_heading=true 时丢掉它的源标题行。
bool merge_with_previous(std::vector<Chapter>& chapters, int index,
                         bool suppress_heading);

// 删除第 index 章；被删的源行区间记入 excluded_ranges 供渲染器跳过。
bool delete_chapter(std::vector<Chapter>& chapters, int index,
                    std::vector<LineRange>& excluded_ranges);

// 在第 index 章的 line 行前拆出新章（line 必须是正文行，不能是标题行）。
bool split_chapter(std::vector<Chapter>& chapters, int index, std::size_t line,
                   std::string_view title, int level);

// 在 end_line 处追加一个空正文章（inserted）。
bool append_chapter(std::vector<Chapter>& chapters, std::size_t end_line,
                    std::string_view title, int level);

// 统一编号：按单位分组重写「第 N 单位 余下标题」，其余标题原样保留。
// pad：阿拉伯数字位数（0=自然，3=001）；chinese 为真时 pad 忽略。
// 跳过 !numbered 与 excluded_from_numbering 的章节，不占用序号。
bool renumber_chapters(std::vector<Chapter>& chapters, int start_number,
                       bool chinese, int pad = 0);

// 两列一起改：number/name 可空但不可同时为空；按 number 是否可解析设置
// numbered。分隔沿用手上的 separator，为空时用单个半角空格。
bool rename_chapter_parts(Chapter& ch, std::string_view number,
                          std::string_view name);

// 只切换「参与编号」标记；仍可移动、仍显示序号列。
void set_excluded_from_numbering(Chapter& ch, bool excluded);

// 把无编号章加入编号：优先占用下一参与编号章的序号并顺延后续同层章；
// 没有下一章时沿用前一章样式 +1；全书无编号时使用「第1章」。
// 保留原章名（例如「序章」），最终标题形如「第1章 序章」。
bool promote_to_numbering(std::vector<Chapter>& chapters, int index);

// 在 line 前拆出新章：当前章保留自己的序号，新章序号=当前+1（同单位/同样式/
// 同分隔/同补零位数），后续参与编号的章节各 +1；不参与/无编号章节原样跳过。
// 当前章无编号或已排除时，新章 number/name 全由参数给出且不移动后续序号。
bool split_chapter_numbered(std::vector<Chapter>& chapters, int index,
                            std::size_t line, std::string_view number,
                            std::string_view name, int level);

// 把当前章正文划给相邻章，当前章只剩标题。正文=主区间 + extra_body。
bool move_body_to_previous(std::vector<Chapter>& chapters, int index);
bool move_body_to_next(std::vector<Chapter>& chapters, int index);

}  // namespace novel
