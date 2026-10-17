#include "ui/Theme.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QFrame>
#include <QLineEdit>
#include <QPalette>
#include <QPushButton>
#include <QScrollBar>
#include <QStyleFactory>
#include <QTableView>
#include <QTextEdit>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <dwmapi.h>
#include <windows.h>
#pragma comment(lib, "dwmapi.lib")
#endif

namespace ui {

// 当前主题色（applyTheme 时更新）。
Colors C;
bool g_dark = true;

namespace {

// 亮色配色（来自设计稿）。
const Colors kLight = {
    QColor(0xF6, 0xF7, 0xF9),  // bg
    QColor(0xFF, 0xFF, 0xFF),  // panel
    QColor(0xEE, 0xF2, 0xF7),  // hover
    QColor(0xDC, 0xE2, 0xEA),  // border
    QColor(0x1B, 0x24, 0x30),  // text
    QColor(0x4E, 0x5A, 0x6E),  // sub（提高对比度）
    QColor(0x3B, 0x6F, 0xE0),  // accent
    QColor(0xE8, 0xF0, 0xFF),  // accentBg
    QColor(0xC2, 0x41, 0x0C),  // invalid
    QColor(0xB4, 0x53, 0x09),  // warning
};

// 深色配色（沿用现有）。
const Colors kDark = {
    QColor(0x0B, 0x0E, 0x14),  // bg
    QColor(0x11, 0x15, 0x1D),  // panel
    QColor(0x16, 0x1B, 0x24),  // hover
    QColor(0x23, 0x2A, 0x36),  // border
    QColor(0xE8, 0xEC, 0xF3),  // text
    QColor(0xA9, 0xB3, 0xC4),  // sub（提高对比度）
    QColor(0x5B, 0x8C, 0xFF),  // accent
    QColor(0x1B, 0x27, 0x40),  // accentBg
    QColor(0xF0, 0x80, 0x5A),  // invalid
    QColor(0xF0, 0xB2, 0x67),  // warning
};

}  // namespace

void applyTheme(QApplication& app, bool dark) {
  g_dark = dark;
  C = dark ? kDark : kLight;

  app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

  QPalette pal;
  pal.setColor(QPalette::Window, C.bg);
  pal.setColor(QPalette::WindowText, C.text);
  pal.setColor(QPalette::Base, C.panel);
  pal.setColor(QPalette::AlternateBase, C.hover);
  pal.setColor(QPalette::Text, C.text);
  pal.setColor(QPalette::Button, C.panel);
  pal.setColor(QPalette::ButtonText, C.text);
  pal.setColor(QPalette::Disabled, QPalette::ButtonText, C.sub);
  pal.setColor(QPalette::Highlight, C.accentBg);
  pal.setColor(QPalette::HighlightedText, C.text);
  pal.setColor(QPalette::ToolTipBase, C.hover);
  pal.setColor(QPalette::ToolTipText, C.text);
  pal.setColor(QPalette::Light, C.border);
  pal.setColor(QPalette::Midlight, C.border);
  pal.setColor(QPalette::Dark, C.border);
  pal.setColor(QPalette::Mid, C.border);
  pal.setColor(QPalette::Shadow, QColor(0, 0, 0));
  app.setPalette(pal);

  app.setStyleSheet(globalStylesheet(dark));
  QFont uiFont(FONT_UI);
  uiFont.setPointSizeF(10.5);
  uiFont.setHintingPreference(QFont::PreferFullHinting);
  uiFont.setStyleStrategy(QFont::PreferAntialias);
  app.setFont(uiFont);
}

QString globalStylesheet(bool dark) {
  const QString a = C.accent.name(QColor::HexRgb);
  const QString aBg = C.accentBg.name(QColor::HexRgb);
  const QString bg = C.bg.name(QColor::HexRgb);
  const QString panel = C.panel.name(QColor::HexRgb);
  const QString hover = C.hover.name(QColor::HexRgb);
  const QString border = C.border.name(QColor::HexRgb);
  const QString text = C.text.name(QColor::HexRgb);
  const QString sub = C.sub.name(QColor::HexRgb);
  const QString invalid = C.invalid.name(QColor::HexRgb);
  const QString warning = C.warning.name(QColor::HexRgb);

  const QString dirtyBg = dark ? QStringLiteral("#2A1F14") : QStringLiteral("#FDF1E7");
  const QString dirtyBorder = dark ? QStringLiteral("#6B4A1E") : QStringLiteral("#F0C9A8");
  const QString dirtyText = dark ? QStringLiteral("#F0B267") : QStringLiteral("#B45309");
  const QString invalidBg = dark ? QStringLiteral("#2A1813") : QStringLiteral("#FBEAE3");
  const QString invalidBorder = dark ? QStringLiteral("#6B3A28") : QStringLiteral("#F0C2AD");
  const QString invalidText = dark ? QStringLiteral("#F0886A") : QStringLiteral("#C2410C");

  // 复选框对勾由 ui::CheckBoxStyle 用 QPainter 自绘（Qt5 不支持 QSS
  // data URI，原 SVG 方案对勾不可见）。这里仅保留底色/描边语义。
  const QStringList vals = {
      bg, text, panel, border, sub, hover, a, aBg, dirtyBg, dirtyText,
      aBg, a, invalidBg, invalidText, dirtyBorder, invalidBorder};

  QString sheet = QStringLiteral(R"(
/* ===== 全局基线 ===== */
QWidget {
  background-color: %1;
  color: %2;
  font-family: "Microsoft YaHei UI", "Microsoft YaHei", "PingFang SC", sans-serif;
  font-size: 10.5pt;
  border: none;
  outline: 0;
}
QLabel { background: transparent; color: %2; }
QFrame { background: transparent; }

/* ===== 顶栏 / 标志 / 标题（objectName 规则，随主题切换） ===== */
QFrame#sidebar { background-color: %1; border-right: 1px solid %4; }
QFrame#customTitleBar { background-color: %3; border-bottom: 1px solid %4; }
QLabel#windowTitle { color: %5; font-size: 10pt; background: transparent; }
QLabel#fileLabel { color: %2; font-size: 10.5pt; background: transparent; }
QPushButton#titleButton {
  background: transparent; border: none; border-radius: 0; padding: 0;
  color: %5; font-size: 13pt;
}
QPushButton#titleButton:hover { background-color: %6; color: %2; }
QPushButton#titleButton:pressed { background-color: %4; }
QPushButton#titleButton[role="close"]:hover {
  background-color: #C42B1C; color: #FFFFFF;
}
QFrame#topBar { background-color: %3; border-bottom: 1px solid %4; }
QFrame#exportBar { background-color: %3; border-bottom: 1px solid %4; }
QLabel#logo {
  background-color: %8; color: %7; border: 1px solid %7;
  border-radius: 8px; font-weight: bold; font-size: 13pt;
}
QLabel#appTitle { font-size: 12pt; font-weight: bold; color: %2; background: transparent; }
QLabel#subTitle { color: %5; font-size: 10.5pt; background: transparent; }
QStatusBar { background-color: %1; color: %5; border-top: 1px solid %4; padding: 2px 4px; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: %2; background: transparent; }

/* ===== chip ===== */
QLabel.chip {
  background-color: %3; color: %5; border: 1px solid %4;
  border-radius: 8px; padding: 3px 10px; font-size: 10.5pt;
}
QLabel.chip#dirtyChip { background-color: %9; color: %10; border: 1px solid %15; font-weight: bold; }
QLabel.chip#warnChip { background-color: %9; color: %10; border: 1px solid %15; }
QLabel.chip#invalidChip { background-color: %7; color: %14; border: 1px solid %16; }

/* ===== 按钮（class 规则，随主题切换） ===== */
QPushButton {
  background-color: %3; color: %2; border: 1px solid %4;
  border-radius: 8px; padding: 3px 10px; font-size: 10.5pt;
}
QPushButton:hover { background-color: %6; border-color: %4; }
QPushButton:pressed { background-color: %6; }
QPushButton:disabled { color: %5; background-color: %3; border-color: %4; }
QPushButton:focus { border-color: %7; }

QPushButton.primary {
  background-color: %8; border: 1px solid %7; color: %7;
}
QPushButton.primary:hover { background-color: %6; border-color: %7; }
QPushButton.primary:pressed { background-color: %8; }
QPushButton.primary:disabled { color: %5; background-color: %3; border-color: %4; }

QPushButton#icon { padding: 3px 6px; font-size: 10.5pt; }

/* ===== 搜索框 ===== */
QLineEdit {
  background-color: %1; border: 1px solid %4; border-radius: 8px;
  padding: 8px 12px; color: %2; font-size: 10.5pt;
}
QLineEdit:focus { border: 1px solid %7; }
QLineEdit:disabled { color: %5; }

/* ===== 列表 ===== */
QTableView {
  background-color: %3; border: 1px solid %4; border-radius: 10px;
  gridline-color: transparent; selection-background-color: transparent;
  selection-color: %2; font-size: 10.5pt; padding: 4px;
}
QTableView::item { border: none; padding: 0; }
QTableView::item:selected { background: transparent; }
QHeaderView { background: transparent; }
QHeaderView::section { background: transparent; border: none; }
QTableView:focus { border: 1px solid %7; }

/* ===== 复选框 ===== */
/* 指示器完全交给 ui::CheckBoxStyle 自绘；这里不要给 indicator 设置任何
   QSS 属性，否则 QStyleSheetStyle 会接管绘制，导致勾选框只剩文字。 */
QCheckBox { background: transparent; spacing: 8px; color: %5; }
QCheckBox::indicator { width: 20px; height: 20px; }
QCheckBox:checked { color: %2; }

/* ===== 目录标题 ===== */
QLabel#dirTitle { font-size: 12pt; font-weight: bold; color: %2; background: transparent; }
QLabel#dirCount { color: %5; font-size: 10.5pt; background: transparent; }

/* ===== 文本预览 ===== */
QFrame#previewCard { background-color: %3; border: 1px solid %4; border-radius: 10px; }
QLabel#previewTitle { font-size: 18pt; font-weight: bold; color: %2; background: transparent; }
QLabel#previewMeta {
  font-family: "Cascadia Mono", Consolas, monospace; font-size: 10pt;
  color: %5; background: transparent;
}
QLabel#emptyLabel { color: %5; font-size: 14pt; background: transparent; }
QFrame#previewWarningBar {
  background-color: %9; border: 1px solid %15; border-radius: 6px;
}
QLabel#previewWarning {
  color: %10; background: transparent; border: none; font-size: 10pt;
}
QPushButton#warningClose {
  background: transparent; color: %10; border: 1px solid %15;
  border-radius: 5px; padding: 1px 7px; font-size: 10pt;
}
QPushButton#warningClose:hover { background-color: %15; color: %2; }
QFrame#rule { background-color: %4; }
QTextEdit {
  background-color: transparent; border: none; padding: 12px 4px 0 0;
  color: %2; font-size: 12pt; selection-background-color: %7;
  selection-color: #FFFFFF;
}

/* ===== 滚动条 ===== */
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical {
  background: %4; border-radius: 5px; min-height: 28px;
}
QScrollBar::handle:vertical:hover { background: %5; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; background: transparent; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: %4; border-radius: 5px; min-width: 28px; }
QScrollBar::handle:horizontal:hover { background: %5; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; background: transparent; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }

/* ===== 提示气泡 ===== */
QToolTip {
  background-color: %3; color: %2; border: 1px solid %4;
  border-radius: 6px; padding: 5px 9px; font-size: 10pt;
}

/* ===== 右键菜单 ===== */
QMenu {
  background-color: %3; border: 1px solid %4; border-radius: 8px;
  padding: 6px; color: %2; font-size: 10.5pt;
}
QMenu::item {
  background: transparent; padding: 7px 18px 7px 26px;
  border-radius: 6px; color: %2;
}
QMenu::item:selected { background-color: %6; }
QMenu::item:disabled { color: %5; }
QMenu::separator { height: 1px; background: %4; margin: 5px 8px; }

/* ===== 自定义对话框 ===== */
QDialog { background-color: %1; color: %2; }
QDialog QLabel { color: %2; }
QDialog QLabel#dialogTitle { color: %2; font-size: 13pt; font-weight: bold; background: transparent; }
QDialog QLabel#dialogHint { color: %5; font-size: 11pt; background: transparent; }

/* ===== 下拉框 / 微调框 ===== */
QComboBox {
  background-color: %1; border: 1px solid %4; border-radius: 8px;
  padding: 6px 10px; color: %2; font-size: 10.5pt; min-height: 18px;
}
QComboBox:hover { border-color: %7; }
QComboBox:focus { border: 1px solid %7; }
QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 22px; border: none; }
QComboBox::down-arrow {
  image: none; border-left: 5px solid transparent; border-right: 5px solid transparent;
  border-top: 6px solid %5; width: 0; height: 0; margin-right: 8px;
}
QComboBox QAbstractItemView {
  background-color: %3; border: 1px solid %4; border-radius: 8px;
  selection-background-color: %6; color: %2; padding: 4px;
}
QSpinBox {
  background-color: %1; border: 1px solid %4; border-radius: 8px;
  padding: 6px 10px; color: %2; font-size: 10.5pt;
}
QSpinBox:hover { border-color: %7; }
QSpinBox:focus { border: 1px solid %7; }
QSpinBox::up-button, QSpinBox::down-button { background: transparent; border: none; width: 18px; }
QSpinBox::up-arrow, QSpinBox::down-arrow { image: none; }

/* ===== 消息框 ===== */
QMessageBox { background-color: %1; color: %2; }
QMessageBox QLabel { color: %2; background: transparent; }
QMessageBox QDialogButtonBox QPushButton { min-width: 84px; }
)");

  // 从大到小替换，避免 "%1" 误中 "%10" 等。
  for (int i = vals.size() - 1; i >= 0; --i) {
    sheet.replace(QStringLiteral("%") + QString::number(i + 1), vals.at(i));
  }
  return sheet;
}

#ifdef _WIN32
void applyWindowsFrame(WId wid, bool dark) {
  const HWND hwnd = reinterpret_cast<HWND>(wid);
  if (!hwnd) return;

  // Win10 的标题栏还受应用级 PreferredAppMode 影响；仅设 DWM 属性在
  // 21H2 暗色系统下可能不生效。uxtheme 序号 135 为 SetPreferredAppMode：
  // 3=ForceLight，2=ForceDark。Win11/无该导出时自动跳过。
  using SetPreferredAppModeFn = int(WINAPI*)(int);
  static const SetPreferredAppModeFn setPreferredAppMode = [] {
    HMODULE ux = GetModuleHandleW(L"uxtheme.dll");
    if (!ux) return static_cast<SetPreferredAppModeFn>(nullptr);
    return reinterpret_cast<SetPreferredAppModeFn>(
        GetProcAddress(ux, MAKEINTRESOURCEA(135)));
  }();
  if (setPreferredAppMode) setPreferredAppMode(dark ? 2 : 3);

  // 深色标题栏：Win11 用 20，Win10 用 19。某些 Win10 版本对 20 返回成功
  // 但实际忽略，因此两个都设置，不能只在 20 失败时才回退。
  const BOOL value = dark ? TRUE : FALSE;
  DwmSetWindowAttribute(hwnd, 20, &value, sizeof(value));
  DwmSetWindowAttribute(hwnd, 19, &value, sizeof(value));

  // Windows 11：显式设置标题栏底色/文字色，避免切回亮色后仍保留系统深色。
  const COLORREF caption =
      dark ? RGB(0x11, 0x15, 0x1D) : RGB(0xF6, 0xF7, 0xF9);
  const COLORREF captionText =
      dark ? RGB(0xE8, 0xEC, 0xF3) : RGB(0x1B, 0x24, 0x30);
  const COLORREF border =
      dark ? RGB(0x23, 0x2A, 0x36) : RGB(0xDC, 0xE2, 0xEA);
  DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
  DwmSetWindowAttribute(hwnd, 36, &captionText, sizeof(captionText));
  DwmSetWindowAttribute(hwnd, 34, &border, sizeof(border));

  // 圆角：DWMWA_WINDOW_CORNER_PREFERENCE=33，值 2 = DWMWCP_ROUND
  const DWORD round = 2;
  DwmSetWindowAttribute(hwnd, 33, &round, sizeof(round));
}
#endif

}  // namespace ui

