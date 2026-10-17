#pragma once

#include <filesystem>
#include <string>

namespace ui {

class MainWindow;

// 自检模式（--selftest）：无窗口交互，返回错误数（0 = 全 PASS）。
// 需要读写 MainWindow 私有状态（撤销栈、预览缓存、无效内容），故声明为友元。
int runSelftest(MainWindow& window, const std::filesystem::path& path,
                const std::string& outDir);

}  // namespace ui
