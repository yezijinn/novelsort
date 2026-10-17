#pragma once

#include <QAbstractTableModel>
#include <vector>

#include "core/Chapter.hpp"

namespace ui {

// 持有章节向量，并以表格模型形式暴露给 QTableView。
// 仅 1 列，行号即章节在“当前顺序”中的下标。
class ChapterModel : public QAbstractTableModel {
  Q_OBJECT

 public:
  explicit ChapterModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {}

  int rowCount(const QModelIndex& parent = QModelIndex()) const override {
    return parent.isValid() ? 0 : static_cast<int>(chapters_.size());
  }
  int columnCount(const QModelIndex& parent = QModelIndex()) const override {
    return parent.isValid() ? 0 : 1;
  }

  QVariant data(const QModelIndex& index, int role) const override;
  Qt::ItemFlags flags(const QModelIndex& index) const override;

  // 以阅读顺序替换全部章节（含合成章）
  void resetChapters(std::vector<novel::Chapter> chapters);
  const std::vector<novel::Chapter>& chapters() const { return chapters_; }
  // 可写引用：直接交给 core 编辑函数原地修改，再调用 refresh()。
  std::vector<novel::Chapter>& chapters() { return chapters_; }

  // 外部原地修改 chapters_ 后刷新视图（行列数可能变化）。
  void refresh();

  // 将 0 基下标 from 处的章节移动到目标下标 to（to 为移动后落点）。
  // 成功返回 true；越界返回 false。会刷新各章节 index 字段。
  bool moveRow(int from, int to);

  void clear();

 private:
  void renumber();

  std::vector<novel::Chapter> chapters_;
};

}  // namespace ui
