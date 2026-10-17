#pragma once

#include <QTableView>

class QDragMoveEvent;
class QDropEvent;
class QDragEnterEvent;
class QPaintEvent;

namespace ui {

// 章节列表拖拽排序：拦截 drop，按源行计算落点后回调，并在拖拽时绘制插入指示线。
class ChapterTableView : public QTableView {
  Q_OBJECT

 public:
  explicit ChapterTableView(QWidget* parent = nullptr);
  ~ChapterTableView() override;

  // 是否允许拖拽（搜索过滤时禁用）。
  void setDragEnabled(bool enabled);

 signals:
  void rowDropped(int fromRow, int toRow);

 protected:
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dropEvent(QDropEvent* event) override;
  void paintEvent(QPaintEvent* event) override;

 private:
  bool dragAllowed_ = true;
  int dropRow_ = -1;  // 插入位置（目标行之前），-1 表示不在列表内
};

}  // namespace ui
