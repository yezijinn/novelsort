#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <QString>
#include <QWidget>

#include "core/Script.hpp"

class QLabel;
class QModelIndex;
class QPushButton;
class QTableView;
class QTextEdit;

namespace ui {

class SegmentTableModel;

// 台词拆分子页面：打开 TXT，调用 core 解析/拆分/校验，
// 以表格 + 详情展示有声书脚本，并可导出 JSON。不含 core 算法。
class DialoguePage : public QWidget {
  Q_OBJECT

 public:
  explicit DialoguePage(QWidget* parent = nullptr);

  // 加载指定 TXT（外壳 --input 或菜单复用）。失败弹出提示。
  void openFile(const std::filesystem::path& path);

  // 当前是否已加载（用于外壳状态）。
  bool hasDocument() const { return !doc_.segments.empty(); }

 private:
  void buildUi();
  void runPipeline(const std::filesystem::path& path);
  void refreshStats();
  void refreshDetail(const QModelIndex& current);
  void onOpenClicked();
  void onReSplitClicked();
  void onExportClicked();
  void showError(const QString& title, const std::string& error);

  SegmentTableModel* model_ = nullptr;
  QTableView* tableView_ = nullptr;
  QTextEdit* detailEdit_ = nullptr;
  QLabel* fileLabel_ = nullptr;
  QLabel* statChapters_ = nullptr;
  QLabel* statSegments_ = nullptr;
  QLabel* statDialogue_ = nullptr;
  QLabel* statThought_ = nullptr;
  QLabel* statWarnings_ = nullptr;
  QPushButton* reSplitBtn_ = nullptr;
  QPushButton* exportBtn_ = nullptr;

  novel::ScriptDocument doc_;
  std::filesystem::path currentPath_;
  std::vector<std::string> validationWarnings_;
};

}  // namespace ui
