#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace novel {

// 解码一个 UTF-8 码点。返回消耗的字节数；非法序列返回 1 且 cp 置 U+FFFD。
std::size_t decode_cp(std::string_view s, std::size_t i, char32_t& cp);

// 追加一个码点的 UTF-8 编码。
void append_utf8(std::string& out, char32_t cp);

// 空白判定：ASCII 空白、不换行空格、各类全角/排版空格、BOM。
bool is_space_cp(char32_t cp);

// 去掉首尾空白，返回视图，不复制。
std::string_view trim_ws_view(std::string_view s);

// 去掉首尾空白，返回副本。
std::string trim_ws(std::string_view s);

// 码点个数。
std::size_t cp_count(std::string_view s);

// 首 / 尾码点；空串返回 0。
char32_t first_cp(std::string_view s);
char32_t last_cp(std::string_view s);

// 按 '\n' 切行，不含换行符。
std::vector<std::string> split_lines(std::string_view text);

// 零拷贝版本：视图指向 text，调用方必须保证其生命周期覆盖视图。
std::vector<std::string_view> split_lines_view(std::string_view text);

// 空行判定：trim 后为空。
bool is_blank_line(std::string_view line);

}  // namespace novel
