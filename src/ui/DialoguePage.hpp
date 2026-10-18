#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <QString>
#include <QWidget>

#include "core/Chapter.hpp"
#include "core/Script.hpp"

class QLabel;
class QListWidget;
class QListWidgetItem;
class QModelIndex;
class QPushButton;
class QSplitter;
class QTableView;
class QTextEdit;

namespace ui {

class SegmentTableModel;

// 台词拆分子页面：打开 TXT，按章懒拆分，逐章展示与导出。不含 core 算法。
class DialoguePage : public QWidget {
  Q_OBJECT

 public:
  explicit DialoguePage(QWidget* parent = nullptr);

  // 加载指定 TXT（外壳 --input 或菜单复用）。失败弹出提示。
  void openFile(const std::filesystem::path& path);

  // 当前是否已加载（用于外壳状态）。
  bool hasDocument() const { return !chapters_.empty(); }

 private:
  struct ChapterMeta {
    std::size_t startLine = 0;
    std::size_t endLine = 0;
    int segments = 0;
    int dialogue = 0;
    int thought = 0;
    int warnings = 0;
  };

  void buildUi();
  // 打开阶段只做 load/parse/填列表/拆分首章。
  void openPipeline(const std::filesystem::path& path);
  // 拆分第 i 章并缓存；已拆分则跳过。
  void buildChapter(int i);
  // 校验单章，返回错误描述。
  std::vector<std::string> validateChapter(int i) const;
  void populateChapterList();
  void selectChapter(int i);
  void refreshChapterStats();
  void refreshStats();
  void refreshDetail(const QModelIndex& current);
  void onChapterSelected(int row);
  void onOpenClicked();
  void onReSplitClicked();
  void onExportClicked();
  void showError(const QString& title, const std::string& error);

  SegmentTableModel* model_ = nullptr;
  QTableView* tableView_ = nullptr;
  QTextEdit* detailEdit_ = nullptr;
  QLabel* fileLabel_ = nullptr;
  QLabel* statChapters_ = nullptr;
  QLabel* statBuilt_ = nullptr;
  QLabel* statSegments_ = nullptr;
  QLabel* statDialogue_ = nullptr;
  QLabel* statThought_ = nullptr;
  QLabel* statWarnings_ = nullptr;
  QPushButton* reSplitBtn_ = nullptr;
  QPushButton* exportBtn_ = nullptr;
  QListWidget* chaptersList_ = nullptr;
  QSplitter* splitter_ = nullptr;

  // 全量解析结果（不持有 segments，只用于拆章与导出）。
  std::vector<novel::Chapter> chapters_;
  std::vector<std::size_t> tocLines_;
  std::string text_;        // 解码后的全文（UTF-8，LF）
  std::string source_;      // 源标识（路径），写入 JSON 的 source 字段
  std::string encoding_;

  // 每章独立缓存，避免整本一次性进表格；外层 vector 在打开时一次性 resize，
  // 保证 model 持有的指针在后续操作中不失效。
  std::vector<std::vector<novel::ScriptSegment>> chapterSegments_;
  std::vector<novel::ScriptChapter> chapterInfo_;
  std::vector<bool> built_;
  std::vector<ChapterMeta> chapterMeta_;

  std::filesystem::path currentPath_;
  int currentChapter_ = -1;
};

}  // namespace ui
