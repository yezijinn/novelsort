#pragma once

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QString>

#include "core/Chapter.hpp"

namespace ui {

// 重命名对话框：序号与标题两列。
struct RenameFields {
  QLineEdit* number = nullptr;
  QLineEdit* name = nullptr;
};

// 拆分对话框。
struct SplitFields {
  QLineEdit* number = nullptr;
  QLineEdit* name = nullptr;
  QCheckBox* participate = nullptr;  // 无编号当前章：新章是否参与排序
  bool autoNumber = false;           // 当前章有编号：序号只读，由 core 自动 +1
};

// 统一编号对话框。
struct RenumberFields {
  QComboBox* style = nullptr;  // 阿拉伯 / 中文
  QSpinBox* start = nullptr;
  QComboBox* pad = nullptr;  // 序号位数：自然 / 2 / 3 / 4
};

// 构建对话框但不执行；调用方负责 exec 与 deleteLater。
// parent 用作 Qt 父子关系与窗口居中，dark 决定标题栏与配色。
QDialog* buildRenameDialog(QWidget* parent, bool dark,
                           const novel::Chapter& chapter, RenameFields& fields);

// suggestedNumber 为无编号章建议占用的序号；为空时默认不勾选参与排序。
QDialog* buildSplitDialog(QWidget* parent, bool dark,
                          const novel::Chapter& chapter,
                          const QString& suggestedNumber, SplitFields& fields);

QDialog* buildRenumberDialog(QWidget* parent, bool dark, RenumberFields& fields);

}  // namespace ui
