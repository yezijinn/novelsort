#include "ui/ChapterTableView.hpp"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QMimeData>
#include <QPainter>
#include <QPoint>

#include "ui/Theme.hpp"

namespace ui {

ChapterTableView::ChapterTableView(QWidget* parent) : QTableView(parent) {
  setRowDragEnabled(true);
  setAcceptDrops(true);
  setDropIndicatorShown(false);  // 自绘指示线
  setDefaultDropAction(Qt::MoveAction);
  setDragDropMode(DragDrop);
  setSelectionMode(SingleSelection);
  setSelectionBehavior(SelectRows);
  setShowGrid(false);
}

ChapterTableView::~ChapterTableView() = default;

void ChapterTableView::setRowDragEnabled(bool enabled) {
  dragAllowed_ = enabled;
  QAbstractItemView::setDragEnabled(enabled);
  setDragDropMode(enabled ? DragDrop : NoDragDrop);
}

void ChapterTableView::dragEnterEvent(QDragEnterEvent* event) {
  const bool itemDrag =
      event->mimeData()->hasFormat(QStringLiteral(
          "application/x-qabstractitemmodeldatalist"));
  if (dragAllowed_ && model() && itemDrag) {
    event->acceptProposedAction();
  } else {
    event->ignore();
  }
}

void ChapterTableView::dragMoveEvent(QDragMoveEvent* event) {
  if (!dragAllowed_ || !model()) {
    event->ignore();
    dropRow_ = -1;
    viewport()->update();
    return;
  }
  event->acceptProposedAction();
  const QModelIndex idx = indexAt(event->pos());
  int row = idx.isValid() ? idx.row() : model()->rowCount();
  // 计算插入位置：落在行下半部则插到该行之后。
  if (idx.isValid()) {
    const QRect r = visualRect(idx);
    if (event->pos().y() - r.top() > r.height() / 2) ++row;
  }
  dropRow_ = row;
  viewport()->update();
}

void ChapterTableView::dropEvent(QDropEvent* event) {
  if (!dragAllowed_ || !model()) {
    event->ignore();
    dropRow_ = -1;
    return;
  }
  const QModelIndex idx = indexAt(event->pos());
  int sourceRow = -1;
  if (idx.isValid()) sourceRow = idx.row();
  // 拖动起点：从选中行取。
  const QModelIndex sel = currentIndex();
  if (sel.isValid()) sourceRow = sel.row();
  if (sourceRow < 0) sourceRow = idx.isValid() ? idx.row() : -1;

  int target = dropRow_;
  dropRow_ = -1;
  viewport()->update();

  if (sourceRow < 0 || target < 0) {
    event->ignore();
    return;
  }
  event->acceptProposedAction();
  emit rowDropped(sourceRow, target);
}

void ChapterTableView::paintEvent(QPaintEvent* event) {
  QTableView::paintEvent(event);
  if (dropRow_ < 0 || !dragAllowed_ || !model() ||
      model()->rowCount() <= 0) {
    return;
  }
  QPainter painter(viewport());
  painter.setRenderHint(QPainter::Antialiasing, true);
  int y = 0;
  const int rc = model() ? model()->rowCount() : 0;
  if (dropRow_ >= rc) {
    const QRect last = visualRect(model()->index(rc - 1, 0));
    y = last.bottom() + 2;
  } else {
    const QRect r = visualRect(model()->index(dropRow_, 0));
    y = r.top() + 2;
  }
  const int x0 = 6;
  const int x1 = viewport()->width() - 6;
  QPen pen(C.accent, 2);
  painter.setPen(pen);
  painter.drawLine(x0, y, x1, y);
  // 端点小三角
  painter.setBrush(C.accent);
  painter.setPen(Qt::NoPen);
  painter.drawEllipse(x0, y - 3, 6, 6);
  painter.drawEllipse(x1 - 6, y - 3, 6, 6);
}

}  // namespace ui
