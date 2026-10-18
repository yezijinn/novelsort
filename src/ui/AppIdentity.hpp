#pragma once

#include <QSettings>
#include <QString>

namespace ui {

// 应用标识：窗口标题、QSettings 组织名 / 应用名、设置项键空间共用一份。
inline constexpr auto kOrgName = "novel-lines";
inline constexpr auto kAppName = "novel-lines";
inline const QString kWindowTitle =
    QStringLiteral("novel-lines · TXT 章节识别与重排");

// 偏好设置统一入口；键空间与主题、导出位置一致。
inline QSettings makeSettings() {
  return QSettings(QString::fromLatin1(kOrgName),
                   QString::fromLatin1(kAppName));
}

}  // namespace ui
