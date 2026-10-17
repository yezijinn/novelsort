#include "ui/ChapterModel.hpp"
#include "ui/Theme.hpp"

#include <algorithm>

namespace ui {

QVariant ChapterModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid()) return {};
  const int row = index.row();
  if (row < 0 || row >= static_cast<int>(chapters_.size())) return {};

  const novel::Chapter& ch = chapters_[static_cast<std::size_t>(row)];
  switch (role) {
    case Qt::DisplayRole:
      return QString::fromStdString(ch.title);
    case RoleNumber:
      return row + 1;  // 显示序号（1 基）
    case RoleLevel:
      return ch.level;
    case RoleSynthetic:
      return ch.synthetic;
    case RoleNumberText:
      // 第一列：有编号显示 number，无编号（序章/番外/无标题）显示空
      return ch.numbered ? QString::fromStdString(ch.number) : QString();
    case RoleTitleText:
      // 第二列：有编号显示 name，无编号显示完整 title
      return ch.numbered ? QString::fromStdString(ch.name)
                         : QString::fromStdString(ch.title);
    case RoleExcluded:
      return ch.excluded_from_numbering;
    default:
      return {};
  }
}

Qt::ItemFlags ChapterModel::flags(const QModelIndex& index) const {
  Qt::ItemFlags result = QAbstractTableModel::flags(index);
  if (index.isValid()) result |= Qt::ItemIsDragEnabled;
  result |= Qt::ItemIsDropEnabled;
  return result;
}

void ChapterModel::refresh() {
  beginResetModel();
  renumber();
  endResetModel();
}

void ChapterModel::resetChapters(std::vector<novel::Chapter> chapters) {
  beginResetModel();
  chapters_ = std::move(chapters);
  renumber();
  endResetModel();
}

void ChapterModel::clear() {
  beginResetModel();
  chapters_.clear();
  endResetModel();
}

void ChapterModel::renumber() { reindex_chapters(chapters_); }

bool ChapterModel::moveRow(int from, int to) {
  const int n = static_cast<int>(chapters_.size());
  if (n == 0) return false;
  if (from < 0 || from >= n) return false;
  if (to < 0 || to >= n) return false;
  if (from == to) return false;

  beginMoveRows(QModelIndex(), from, from, QModelIndex(), to > from ? to + 1 : to);
  novel::Chapter moved = std::move(chapters_[static_cast<std::size_t>(from)]);
  chapters_.erase(chapters_.begin() + from);
  chapters_.insert(chapters_.begin() + to, std::move(moved));
  endMoveRows();

  renumber();
  emit dataChanged(index(0, 0), index(n - 1, 0), {RoleNumber});
  return true;
}

}  // namespace ui
