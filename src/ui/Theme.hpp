#pragma once

#include <QApplication>
#include <QColor>
#include <QString>

namespace ui {

// 配色（可切换明/暗）。applyTheme 时按 dark 重新填充 C。
struct Colors {
  QColor bg;          // 背景
  QColor panel;       // 面板 / 列表 / 卡片
  QColor hover;       // 悬浮
  QColor border;      // 边框
  QColor text;        // 正文
  QColor sub;         // 次要
  QColor accent;      // 强调（文字/强调条）
  QColor accentBg;    // 强调底（选中行）
  QColor invalid;     // 无效内容
  QColor warning;     // 警告
};

extern Colors C;
extern bool g_dark;

// 字体族
inline const QString FONT_UI = QStringLiteral("\"Microsoft YaHei UI\", \"Microsoft YaHei\", \"PingFang SC\", sans-serif");
inline const QString FONT_MONO = QStringLiteral("\"Cascadia Mono\", Consolas, \"Courier New\", monospace");

// 应用主题：Fusion 样式 + QPalette + 全局 QSS。dark=true 用深色，false 用亮色。
void applyTheme(QApplication& app, bool dark);

// 全局样式表（按 dark 返回两套之一）。
QString globalStylesheet(bool dark);

// 自定义模型角色
enum ModelRoles {
  RoleNumber = Qt::UserRole + 1,   // 当前序号（1 基）
  RoleLevel = Qt::UserRole + 2,    // 1=卷/部/篇 2=章/节/话
  RoleSynthetic = Qt::UserRole + 3, // 是否为合成章
  RoleRenamed = Qt::UserRole + 4,  // 用户改过标题
  RoleMerged = Qt::UserRole + 5,   // 合并产生的章节（正文并入）
  RoleInserted = Qt::UserRole + 6, // 拆分/新增插入的章节
  RoleNumberText = Qt::UserRole + 7, // 第一列：序号文本（number；无编号为空）
  RoleTitleText = Qt::UserRole + 8,  // 第二列：标题文本（有编号=name；无编号=title）
  RoleExcluded = Qt::UserRole + 9,   // 是否排除于编号（不参与排序）
  RoleNumbered = Qt::UserRole + 10   // 是否被解析为可递增序号
};

#ifdef _WIN32
// 设置 Windows 深色标题栏（DWMWA_USE_IMMERSIVE_DARK_MODE=20/19）+ Win11 圆角
// （DWMWA_WINDOW_CORNER_PREFERENCE=33，值 2=圆角）。失败静默。
void applyWindowsFrame(WId wid, bool dark);
#endif

}  // namespace ui
