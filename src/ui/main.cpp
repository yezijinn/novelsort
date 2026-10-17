#include <QApplication>
#include <QByteArray>
#include <QCommandLineParser>
#include <QDesktopWidget>
#include <QDir>
#include <QMainWindow>
#include <QMessageBox>
#include <QSettings>
#include <QTimer>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>

#include "ui/MainWindow.hpp"
#include "ui/Selftest.hpp"
#include "core/TextUtf8.hpp"
#include "ui/Theme.hpp"

namespace {

bool is_batch_mode(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--selftest") return true;
  }
  return false;
}
}  // namespace

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifdef _WIN32
namespace {

// 发布目录必须带 platforms\qwindows.dll；缺失时给出可执行的修复提示，
// 避免 Qt 在 QApplication 构造阶段直接弹“could not find or load ... in ''”。
bool has_windows_platform_plugin() {
  wchar_t buffer[MAX_PATH * 4] = {};
  constexpr DWORD kBufferLength =
      static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
  const DWORD length = GetModuleFileNameW(nullptr, buffer, kBufferLength);
  if (length == 0 || length >= kBufferLength) return true;  // 无法判断时不拦截
  const std::filesystem::path exe_dir =
      std::filesystem::path(std::wstring(buffer, length)).parent_path();
  std::error_code ec;
  if (std::filesystem::exists(exe_dir / L"platforms" / L"qwindows.dll", ec)) {
    return true;
  }
  MessageBoxW(
      nullptr,
      L"缺少 Qt 平台插件：\nplatforms\\qwindows.dll\n\n"
      L"请运行 deploy-gui 目标，或使用完整的发布目录；不要把 exe 单独复制出来。",
      L"novel-lines 启动失败", MB_OK | MB_ICONERROR);
  return false;
}

}  // namespace
#endif

int main(int argc, char** argv) {
  const bool batch = is_batch_mode(argc, argv);
#ifdef _WIN32
  if (!has_windows_platform_plugin()) return 1;
  // 某些测试脚本会把 QT_QPA_PLATFORM 留在 offscreen/minimal；普通双击启动
  // 必须强制回到 windows， 否则窗口不出现，或缺少 offscreen 插件时直接报错。
  if (!batch) {
    const QByteArray platform = qgetenv("QT_QPA_PLATFORM");
    if (platform == "offscreen" || platform == "minimal") {
      qputenv("QT_QPA_PLATFORM", "windows");
    }
  }
#endif

  // 高 DPI 必须在 QApplication 构造前设置；截图模式关闭缩放，
  // 保证截图尺寸等于窗口逻辑尺寸。
  if (!batch) {
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);
  }

  QApplication app(argc, argv);
  QApplication::setApplicationName(QStringLiteral("novel-lines"));
  QApplication::setOrganizationName(QStringLiteral("novel-lines"));

  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral("TXT 章节识别与重排（桌面版）"));
  QCommandLineOption inputOpt(QStringLiteral("input"),
                              QStringLiteral("启动时加载的 TXT 文件"),
                              QStringLiteral("file"));
  QCommandLineOption selftestOpt(QStringLiteral("selftest"),
                                  QStringLiteral("无窗口自检：加载 --input 文件，依次执行编辑操作并导出到 %TEMP%"),
                                  QStringLiteral("outdir"));
  parser.addOption(inputOpt);
  parser.addOption(selftestOpt);
  parser.process(app);

  // 启动时按 QSettings 持久化的主题（默认暗色）应用一次。
  QSettings settings(QStringLiteral("novel-lines"), QStringLiteral("novel-lines"));
  const QString savedTheme =
      settings.value(QStringLiteral("theme"), QStringLiteral("dark")).toString();
  const bool dark = (savedTheme != QStringLiteral("light"));
  ui::applyTheme(app, dark);

  // 自检模式：无窗口交互，直接执行并返回退出码
  const QString selftest = parser.value(selftestOpt);
  if (!selftest.isEmpty()) {
    const QString input = parser.value(inputOpt);
    if (input.isEmpty()) {
      std::fprintf(stderr, "自检模式需要 --input 指定测试文件\n");
      return 1;
    }
    ui::MainWindow window;
    const int failures = ui::runSelftest(
        window, novel::path_from_utf8(input.toStdString()),
        novel::path_to_utf8(novel::path_from_utf8(selftest.toStdString())));
    return failures == 0 ? 0 : 1;
  }

  ui::MainWindow window;
  // 居中必须在 resize 之后：构造时拿到的还是默认尺寸。
  window.show();
  window.move(QApplication::desktop()->availableGeometry(&window).center() -
              QPoint(window.width() / 2, window.height() / 2));
  window.activateWindow();

#ifdef _WIN32
  // Windows 深色标题栏 + 圆角（与启动时主题一致）
  ui::applyWindowsFrame(window.winId(), dark);
#endif

  const bool recovered = window.tryRecoverBackup();
  const QString input = parser.value(inputOpt);
  if (!input.isEmpty() && !recovered) {
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QApplication::processEvents();
    window.loadFile(novel::path_from_utf8(input.toStdString()), /*silent=*/false);
    QApplication::restoreOverrideCursor();
  }

  return app.exec();
}
