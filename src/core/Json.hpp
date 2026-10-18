#pragma once

#include <string>
#include <string_view>

namespace novel {

// 追加一个 JSON 字符串字面量（含首尾引号），控制字符按 JSON 规则转义。
void append_json_string(std::string& out, std::string_view text);

// 追加一个整数。
void append_json_number(std::string& out, long long value);

// 追加定点小数；非有限值回落为 0，保证不输出 NaN/Inf。
void append_json_fixed(std::string& out, double value, int decimals);

}  // namespace novel
