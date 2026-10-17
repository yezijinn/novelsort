#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <QDateTime>
#include <QString>

#include "core/Chapter.hpp"

namespace ui {

// 自动备份内容：章节状态、编辑区间与源文件身份。
// 不保存原文全文，只保存恢复所需的最小状态。
struct RecoveryData {
  QString sourcePath;
  qint64 sourceSize = 0;
  qint64 sourceMTime = 0;
  qint64 savedAtMs = 0;
  QString encoding;
  bool acceptUnnumbered = false;
  int selectedSourceRow = -1;
  std::vector<novel::Chapter> chapters;
  std::vector<novel::LineRange> excluded;
  std::vector<novel::TextExclusion> exclusions;
};

// 备份文件为持久化格式；增删字段必须同时升 kRecoveryVersion。
QString recoveryFilePath();

// 原子写入：失败时旧备份保持不变。
bool writeRecovery(const QString& path, const RecoveryData& data);

// 读取失败返回 false，调用方应删除损坏文件。
bool readRecovery(const QString& path, RecoveryData& data);

}  // namespace ui
