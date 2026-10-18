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
    QColor(0xF6, 0xF7, 0xF9),  // caption
    QColor(0x1B, 0x24, 0x30),  // captionText
    QColor(0xDC, 0xE2, 0xEA),  // captionBorder
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
    QColor(0x11, 0x15, 0x1D),  // caption
    QColor(0xE8, 0xEC, 0xF3),  // captionText
    QColor(0x23, 0x2A, 0x36),  // captionBorder
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
  // token -> 颜色：按名引用，顺序变化不会错位。
  const QHash<QString, QString> colors = {
      {QStringLiteral("bg"), bg},
      {QStringLiteral("text"), text},
      {QStringLiteral("panel"), panel},
      {QStringLiteral("border"), border},
      {QStringLiteral("sub"), sub},
      {QStringLiteral("hover"), hover},
      {QStringLiteral("accent"), a},
      {QStringLiteral("accentBg"), aBg},
      {QStringLiteral("dirtyBg"), dirtyBg},
      {QStringLiteral("dirtyText"), dirtyText},
      {QStringLiteral("dirtyBorder"), dirtyBorder},
      {QStringLiteral("invalidBg"), invalidBg},
      {QStringLiteral("invalidText"), invalidText},
      {QStringLiteral("invalidBorder"), invalidBorder},
      {QStringLiteral("warning"), warning},
      {QStringLiteral("invalid"), invalid}};

  QString sheet = QStringLiteral(R"(
/* ===== 全局基线 ===== */
QWidget {
  background-color: %{bg};
  color: %{text};
  font-family: "Microsoft YaHei UI", "Microsoft YaHei", "PingFang SC", sans-serif;
  font-size: 10.5pt;
  border: none;
  outline: 0;
}
QLabel { background: transparent; color: %{text}; }
QFrame { background: transparent; }

/* ===== 顶栏 / 标志 / 标题（objectName 规则，随主题切换） ===== */
QFrame#sidebar { background-color: %{bg}; border-right: 1px solid %{border}; }
QFrame#customTitleBar { background-color: %{panel}; border-bottom: 1px solid %{border}; }
QLabel#windowTitle { color: %{sub}; font-size: 10pt; background: transparent; }
QLabel#fileLabel { color: %{text}; font-size: 10.5pt; background: transparent; }
QPushButton#titleButton {
  background: transparent; border: none; border-radius: 0; padding: 0;
  color: %{sub}; font-size: 13pt;
}
QPushButton#titleButton:hover { background-color: %{hover}; color: %{text}; }
QPushButton#titleButton:pressed { background-color: %{border}; }
QPushButton#titleButton[role="close"]:hover {
  background-color: #C42B1C; color: #FFFFFF;
}
QFrame#topBar { background-color: %{panel}; border-bottom: 1px solid %{border}; }
QFrame#exportBar { background-color: %{panel}; border-bottom: 1px solid %{border}; }

/* ===== 外壳侧栏 ===== */
QFrame#appSidebar { background-color: %{bg}; border-right: 1px solid %{border}; }
QLabel#appName { font-size: 16pt; font-weight: bold; color: %{text}; background: transparent; }
QLabel#appHint { font-size: 10.5pt; color: %{sub}; background: transparent; }
QListWidget#navList {
  background-color: %{bg}; border: none; border-radius: 10px;
  padding: 4px; font-size: 11pt; outline: 0;
}
QListWidget#navList::item {
  background-color: transparent; color: %{text}; border-radius: 8px;
  padding: 9px 12px; margin: 2px 0;
}
QListWidget#navList::item:hover { background-color: %{hover}; }
QListWidget#navList::item:selected {
  background-color: %{accentBg}; color: %{accent}; font-weight: bold;
}
QListWidget#navList::item:selected:!active { background-color: %{accentBg}; color: %{accent}; }
QMenuBar {
  background-color: %{panel}; border-bottom: 1px solid %{border};
  color: %{text}; font-size: 10.5pt; padding: 2px;
}
QMenuBar::item { background: transparent; padding: 5px 12px; border-radius: 6px; }
QMenuBar::item:selected { background-color: %{hover}; }
QMenuBar::item:pressed { background-color: %{border}; }
QLabel#logo {
  background-color: %{accentBg}; color: %{accent}; border: 1px solid %{accent};
  border-radius: 8px; font-weight: bold; font-size: 13pt;
}
QLabel#appTitle { font-size: 12pt; font-weight: bold; color: %{text}; background: transparent; }
QLabel#subTitle { color: %{sub}; font-size: 10.5pt; background: transparent; }
QStatusBar { background-color: %{bg}; color: %{sub}; border-top: 1px solid %{border}; padding: 2px 4px; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: %{text}; background: transparent; }

/* ===== chip ===== */
QLabel.chip {
  background-color: %{panel}; color: %{sub}; border: 1px solid %{border};
  border-radius: 8px; padding: 3px 10px; font-size: 10.5pt;
}
QLabel.chip#dirtyChip { background-color: %{dirtyBg}; color: %{dirtyText}; border: 1px solid %{dirtyBorder}; font-weight: bold; }
QLabel.chip#warnChip { background-color: %{dirtyBg}; color: %{dirtyText}; border: 1px solid %{dirtyBorder}; }
QLabel.chip#invalidChip { background-color: %{accent}; color: %{invalidText}; border: 1px solid %{invalidBorder}; }

/* ===== 按钮（class 规则，随主题切换） ===== */
QPushButton {
  background-color: %{panel}; color: %{text}; border: 1px solid %{border};
  border-radius: 8px; padding: 3px 10px; font-size: 10.5pt;
}
QPushButton:hover { background-color: %{hover}; border-color: %{border}; }
QPushButton:pressed { background-color: %{hover}; }
QPushButton:disabled { color: %{sub}; background-color: %{panel}; border-color: %{border}; }
QPushButton:focus { border-color: %{accent}; }

QPushButton.primary {
  background-color: %{accentBg}; border: 1px solid %{accent}; color: %{accent};
}
QPushButton.primary:hover { background-color: %{hover}; border-color: %{accent}; }
QPushButton.primary:pressed { background-color: %{accentBg}; }
QPushButton.primary:disabled { color: %{sub}; background-color: %{panel}; border-color: %{border}; }

QPushButton#icon { padding: 3px 6px; font-size: 10.5pt; }

/* ===== 搜索框 ===== */
QLineEdit {
  background-color: %{bg}; border: 1px solid %{border}; border-radius: 8px;
  padding: 8px 12px; color: %{text}; font-size: 10.5pt;
}
QLineEdit:focus { border: 1px solid %{accent}; }
QLineEdit:disabled { color: %{sub}; }

/* ===== 列表 ===== */
QTableView {
  background-color: %{panel}; border: 1px solid %{border}; border-radius: 10px;
  gridline-color: transparent; selection-background-color: transparent;
  selection-color: %{text}; font-size: 10.5pt; padding: 4px;
}
QTableView::item { border: none; padding: 0; }
QTableView::item:selected { background: transparent; }
QTableView#dialogueTable::item:selected {
  background-color: %{accentBg}; color: %{text};
}
QTableView#dialogueTable::item:hover {
  background-color: %{hover};
}
QHeaderView { background: transparent; }
QHeaderView::section { background: transparent; border: none; }
QTableView:focus { border: 1px solid %{accent}; }

/* ===== 复选框 ===== */
/* 指示器完全交给 ui::CheckBoxStyle 自绘；这里不要给 indicator 设置任何
   QSS 属性，否则 QStyleSheetStyle 会接管绘制，导致勾选框只剩文字。 */
QCheckBox { background: transparent; spacing: 8px; color: %{sub}; }
QCheckBox::indicator { width: 20px; height: 20px; }
QCheckBox:checked { color: %{text}; }

/* ===== 目录标题 ===== */
QLabel#dirTitle { font-size: 12pt; font-weight: bold; color: %{text}; background: transparent; }
QLabel#dirCount { color: %{sub}; font-size: 10.5pt; background: transparent; }

/* ===== 文本预览 ===== */
QFrame#previewCard { background-color: %{panel}; border: 1px solid %{border}; border-radius: 10px; }
QLabel#previewTitle { font-size: 18pt; font-weight: bold; color: %{text}; background: transparent; }
QLabel#previewMeta {
  font-family: "Cascadia Mono", Consolas, monospace; font-size: 10pt;
  color: %{sub}; background: transparent;
}
QLabel#emptyLabel { color: %{sub}; font-size: 14pt; background: transparent; }
QFrame#previewWarningBar {
  background-color: %{dirtyBg}; border: 1px solid %{dirtyBorder}; border-radius: 6px;
}
QLabel#previewWarning {
  color: %{dirtyText}; background: transparent; border: none; font-size: 10pt;
}
QPushButton#warningClose {
  background: transparent; color: %{dirtyText}; border: 1px solid %{dirtyBorder};
  border-radius: 5px; padding: 1px 7px; font-size: 10pt;
}
QPushButton#warningClose:hover { background-color: %{dirtyBorder}; color: %{text}; }
QFrame#rule { background-color: %{border}; }
QTextEdit {
  background-color: transparent; border: none; padding: 12px 4px 0 0;
  color: %{text}; font-size: 12pt; selection-background-color: %{accent};
  selection-color: #FFFFFF;
}

/* ===== 滚动条 ===== */
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical {
  background: %{border}; border-radius: 5px; min-height: 28px;
}
QScrollBar::handle:vertical:hover { background: %{sub}; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; background: transparent; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: %{border}; border-radius: 5px; min-width: 28px; }
QScrollBar::handle:horizontal:hover { background: %{sub}; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; background: transparent; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }

/* ===== 提示气泡 ===== */
QToolTip {
  background-color: %{panel}; color: %{text}; border: 1px solid %{border};
  border-radius: 6px; padding: 5px 9px; font-size: 10pt;
}

/* ===== 右键菜单 ===== */
QMenu {
  background-color: %{panel}; border: 1px solid %{border}; border-radius: 8px;
  padding: 6px; color: %{text}; font-size: 10.5pt;
}
QMenu::item {
  background: transparent; padding: 7px 18px 7px 26px;
  border-radius: 6px; color: %{text};
}
QMenu::item:selected { background-color: %{hover}; }
QMenu::item:disabled { color: %{sub}; }
QMenu::separator { height: 1px; background: %{border}; margin: 5px 8px; }

/* ===== 自定义对话框 ===== */
QDialog { background-color: %{bg}; color: %{text}; }
QDialog QLabel { color: %{text}; }
QDialog QLabel#dialogTitle { color: %{text}; font-size: 13pt; font-weight: bold; background: transparent; }
QDialog QLabel#dialogHint { color: %{sub}; font-size: 11pt; background: transparent; }

/* ===== 下拉框 / 微调框 ===== */
QComboBox {
  background-color: %{bg}; border: 1px solid %{border}; border-radius: 8px;
  padding: 6px 10px; color: %{text}; font-size: 10.5pt; min-height: 18px;
}
QComboBox:hover { border-color: %{accent}; }
QComboBox:focus { border: 1px solid %{accent}; }
QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: top right; width: 22px; border: none; }
QComboBox::down-arrow {
  image: none; border-left: 5px solid transparent; border-right: 5px solid transparent;
  border-top: 6px solid %{sub}; width: 0; height: 0; margin-right: 8px;
}
QComboBox QAbstractItemView {
  background-color: %{panel}; border: 1px solid %{border}; border-radius: 8px;
  selection-background-color: %{hover}; color: %{text}; padding: 4px;
}
QSpinBox {
  background-color: %{bg}; border: 1px solid %{border}; border-radius: 8px;
  padding: 6px 10px; color: %{text}; font-size: 10.5pt;
}
QSpinBox:hover { border-color: %{accent}; }
QSpinBox:focus { border: 1px solid %{accent}; }
QSpinBox::up-button, QSpinBox::down-button { background: transparent; border: none; width: 18px; }
QSpinBox::up-arrow, QSpinBox::down-arrow { image: none; }

/* ===== 消息框 ===== */
QMessageBox { background-color: %{bg}; color: %{text}; }
QMessageBox QLabel { color: %{text}; background: transparent; }
QMessageBox QDialogButtonBox QPushButton { min-width: 84px; }
)");

  // 按 token 名替换：%{bg} 这类占位符不受插入顺序影响。
  for (auto it = colors.constBegin(); it != colors.constEnd(); ++it) {
    sheet.replace(QStringLiteral("%{") + it.key() + QStringLiteral("}"),
                  it.value());
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
  // 取自当前主题配色，避免同一组颜色在两处各写一份。
  const COLORREF caption = RGB(C.caption.red(), C.caption.green(), C.caption.blue());
  const COLORREF captionText =
      RGB(C.captionText.red(), C.captionText.green(), C.captionText.blue());
  const COLORREF border =
      RGB(C.captionBorder.red(), C.captionBorder.green(), C.captionBorder.blue());
  DwmSetWindowAttribute(hwnd, 35, &caption, sizeof(caption));
  DwmSetWindowAttribute(hwnd, 36, &captionText, sizeof(captionText));
  DwmSetWindowAttribute(hwnd, 34, &border, sizeof(border));

  // 圆角：DWMWA_WINDOW_CORNER_PREFERENCE=33，值 2 = DWMWCP_ROUND
  const DWORD round = 2;
  DwmSetWindowAttribute(hwnd, 33, &round, sizeof(round));
}
#endif

}  // namespace ui

