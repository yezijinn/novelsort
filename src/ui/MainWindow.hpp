#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <QMainWindow>
#include <QSortFilterProxyModel>
#include <QString>
#include <QTextCursor>
#include <QVector>

#include "core/Chapter.hpp"
#include "ui/ChapterDialogs.hpp"
#include "ui/ChapterModel.hpp"
#include "ui/RecoveryStore.hpp"

class QTextEdit;
class QTableView;
class QTextCursor;
class QLineEdit;
class QCheckBox;
class QLabel;
class QPushButton;
class QSizeGrip;
class QStatusBar;
class QTimer;
class QComboBox;
class QSpinBox;
class QDialog;
class QDragEnterEvent;
class QDropEvent;
class QCloseEvent;
class QShowEvent;
class QEvent;
class QResizeEvent;
class QMenu;

namespace ui {

class ChapterTableView;
class PreviewEdit;

class MainWindow : public QMainWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

  // 自检需要读写私有状态（撤销栈、预览缓存、无效内容、对话框字段）。
  friend int runSelftest(MainWindow& window, const std::filesystem::path& path,
                         const std::string& outDir);

  // 加载文件；调用方负责在替换当前内容前确认未保存修改。
  // silent=true 时仅更新空状态，不弹加载错误框。
  void loadFile(const std::filesystem::path& path, bool silent = false);

  // 启动时检查自动备份；返回 true 表示已恢复，调用方不要再加载 --input。
  bool tryRecoverBackup();

  // 切换明/暗主题（顶栏按钮调用）。
  void toggleTheme();
  void applyThemeToWindow(bool dark);

 protected:
  void showEvent(QShowEvent* event) override;
  void changeEvent(QEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dropEvent(QDropEvent* event) override;
  void closeEvent(QCloseEvent* event) override;

 private slots:
  void onOpenClicked();
  void onSelectionChanged(const QModelIndex& current, const QModelIndex& previous);
  void onSearchChanged(const QString& text);
  void onMoveUp();
  void onMoveDown();
  void onResetOrder();
  void onAcceptUnnumberedToggled(bool checked);
  void onExportTxt();
  void onExportJson();
  void onOpenExportLocation();
  void onUndo();
  void onRedo();
  void onRenameChapter();
  void onToggleExcluded(bool excluded);
  void onJoinNumbering();
  void onMoveBodyToPrevious();
  void onMoveBodyToNext();
  void onDeleteChapter();
  void onRenumber();
  void onListContextMenu(const QPoint& pos);
  void onPreviewContextMenu(const QPoint& pos);
  void onSplitHere();
  void onSetInvalid();
  void onRowDropped(int fromRow, int toRow);

 private:
  void buildUi();
  bool confirmDiscardChanges();
  QString recoveryFilePath() const;
  bool writeRecoveryBackup();
  bool readRecoveryBackup(RecoveryData& backup) const;
  void clearRecoveryBackup();
  void scheduleRecoveryBackup();
  void applyModel();
  void previewSourceRow(int sourceRow);
  void updateControls();
  void updateChips();
  void updatePreviewWarning();
  void rebuildLossyLines();
  void onWarningClicked();
  void jumpToNextLossy();
  void updateStatusBar();
  void setEmptyState();
  void refreshSelectionAfterMove(int newSourceRow);
  void showLoadError(const std::string& error);

  // 导出实现：json=false 写 TXT（遵循「导出到程序位置」勾选），true 写目录 JSON。
  // 成功后更新 lastExportedPath_ 并启用「打开导出位置」。
  bool exportContent(bool json);
  // 解析导出目标路径；为空表示用户取消。
  QString resolveExportPath(bool json);
  // 目标已存在时询问处理方式；返回 false 表示取消。
  // 选「另存为」时把新路径写入 exportSaveAsPath_。
  bool confirmExportOverwrite(const QString& path);
  // 原子写入；失败时旧文件保持原样。
  bool writeExportFile(const QString& path, const std::string& content);

  // 预览区光标所在位置映射到源行号。
  int previewCursorToSource(QTextCursor cursor) const;

  // 在预览文档上叠加「无效内容」删除线样式（文档保留，仅视觉标记）。
  void applyExclusionFormats(const QVector<int>& sourceToBlock);

  // 撤销 / 重做快照。自检需要直接构造快照验证内存裁剪，故为公开类型。
  struct Snapshot {
    std::vector<novel::Chapter> chapters;
    std::vector<novel::LineRange> excluded;
    std::vector<novel::TextExclusion> exclusions;  // 无效内容片段
    int selectedSourceRow;
    std::size_t bytes = 0;  // 快照建立时算好，裁剪时不再重复扫描章节
  };
  // 估算一份快照实际占用的堆内存（字符串按 capacity 保守计算）。
  static std::size_t snapshotBytes(const Snapshot& snap);
  // 同时受步数与总内存约束，超限从最旧一步开始丢；至少保留一步。
  static void trimUndoStack(std::vector<Snapshot>& stack,
                            std::size_t stepLimit, std::size_t byteLimit);

 private:
  void pushUndo();
  void undo();
  void redo();
  void applySnapshot(const Snapshot& snap);

  // 修改状态
  void setDirty(bool dirty);
  void markEdited();

  // 当前选中源行（无则 -1）
  int currentSourceRow() const;

  // 目录右键菜单构建
  QMenu* buildChapterMenu(bool onItem, int row);

  // 当前选中章（无则 nullptr）
  const novel::Chapter* currentChapter() const;

  // 对话框由 ui::ChapterDialogs 构建，字段类型见 ui/ChapterDialogs.hpp。
  void renumberDialog();

  // 模型与视图
  ChapterModel* chapterModel_ = nullptr;
  QSortFilterProxyModel* proxy_ = nullptr;
  ChapterTableView* tableView_ = nullptr;
  QLineEdit* searchEdit_ = nullptr;
  PreviewEdit* previewEdit_ = nullptr;
  QLabel* previewTitle_ = nullptr;
  QLabel* previewMeta_ = nullptr;
  QLabel* previewWarning_ = nullptr;  // 编码/解析警告条文本
  QWidget* previewWarningBar_ = nullptr;  // 警告条容器（含关闭按钮）
  bool warningBannerDismissed_ = false;
  QLabel* emptyLabel_ = nullptr;
  QWidget* previewHead_ = nullptr;
  QCheckBox* acceptUnnumbered_ = nullptr;  // 状态存储（界面用按钮显示）
  QPushButton* acceptUnnumberedButton_ = nullptr;
  QPushButton* exportToExecDirButton_ = nullptr;
  void updateAcceptUnnumberedButton();
  void updateExportToExecDirButton();

  // 顶栏 / 底栏
  QLabel* fileLabel_ = nullptr;  // 当前文件名（完整路径放 tooltip）
  QLabel* encodingChip_ = nullptr;
  QLabel* countChip_ = nullptr;
  QLabel* statusLabel_ = nullptr;
  QLabel* metaChip_ = nullptr;   // 编码
  QLabel* tocChip_ = nullptr;    // 目录区
  QLabel* warnChip_ = nullptr;   // 警告
  QLabel* dirtyChip_ = nullptr;  // 已修改
  QPushButton* themeBtn_ = nullptr;  // 主题切换按钮（固定文案「明暗主题」）
  QPushButton* titleMax_ = nullptr;  // 自绘标题栏最大化/还原
  QSizeGrip* sizeGrip_ = nullptr;    // 无边框窗口右下角缩放
  QTimer* recoveryTimer_ = nullptr;  // 自动备份节流定时器
  bool recoveryEnabled_ = true;
  bool recovering_ = false;
  void updateTitleBarButtons();

  // 按钮
  struct Buttons;
  std::unique_ptr<Buttons> btns_;
  bool themeDark_ = true;

  // 导出相关：记忆「导出到程序位置」勾选；记录最近一次导出路径用于「打开位置」
  QCheckBox* exportToExecDir_ = nullptr;  // 状态存储（界面用按钮显示）
  QString lastExportedPath_;             // 最近一次成功导出的完整路径
  QString s_exportMruDir;                // 保存框最近使用的目录
  QString exportSaveAsPath_;             // 「另存为」选中的新路径

  // 加载内容
  std::string loadedText_;             // UTF-8，LF
  std::string loadedEncoding_;
  std::vector<novel::Chapter> initialChapters_;  // 初始解析顺序（用于重置）
  std::vector<std::size_t> tocLines_;
  std::vector<std::string> warnings_;
  std::vector<std::string> encodingWarnings_;  // 加载阶段的编码提示，重解析时保留
  std::vector<std::size_t> lossyLines_;        // 含 U+FFFD 的源行号（升序去重）
  std::size_t lossyJumpIndex_ = 0;             // 连续点击定位的游标
  // 预览用的源行视图（零拷贝，指向 loadedText_ 内部）。
  // 注意：lines_ 是 string_view 容器，其底层存储为 loadedText_；
  // 只要 loadedText_ 在本次文件会话中不被重新分配/修改，这些视图就有效。
  // loadFile / runSelfTest 中：先确定 loadedText_，再 split_lines_view，
  // 且之后只对 loadedText_ 整体替换（clear + 重新赋值）而绝不在原缓冲上修改，
  // 因此视图在整个文件会话期间不会出现悬垂。
  std::vector<std::string_view> lines_;
  std::filesystem::path currentFile_;
  bool isLoaded_ = false;
  bool dirty_ = false;                 // 已编辑但未导出

  // 删除章节时跳过的源行区间
  std::vector<novel::LineRange> excludedRanges_;

  // 无效内容片段（鼠标选中后标记，导出时丢弃对应字节，文档中仍保留）。
  std::vector<novel::TextExclusion> textExclusions_;

  // 预览映射：实际加入预览的源行（跳过标题/toc/skip_lines）逐行记录
  QVector<int> previewLineToSource_;

  // 预览单条缓存：仅保留最近一条，键不变时只刷新标题/元信息。
  // 键 = (sourceRow, start_line, end_line, excludedRanges_ 哈希, skip_lines 哈希,
  //       textExclusions_ 哈希, extra_body 哈希, themeDark_)
  struct PreviewCache {
    int sourceRow = -1;
    std::size_t startLine = 0;
    std::size_t endLine = 0;
    std::size_t excludedHash = 0;
    std::size_t skipHash = 0;
    std::size_t exclusionHash = 0;
    std::size_t extraHash = 0;
    bool themeDark = true;
    QString body;
    // 缓存两份映射：block -> 源行（光标定位、拆分用）与 源行 -> block
    // （无效内容删除线定位用）。缺一份时缓存命中无法还原删除线状态。
    QVector<int> previewLineToSource;
    QVector<int> previewSourceToBlock;

    bool empty() const { return sourceRow < 0; }
    void reset() { *this = PreviewCache{}; }
    // 键未变（如仅改名/改层级/重编号）时返回 true，可复用 body。
    bool matches(int sr, std::size_t sl, std::size_t el, std::size_t ex,
                 std::size_t sk, std::size_t excl, std::size_t extra,
                 bool dark) const {
      return sourceRow == sr && startLine == sl && endLine == el &&
             excludedHash == ex && skipHash == sk &&
             exclusionHash == excl && extraHash == extra && themeDark == dark;
    }
  };
  PreviewCache previewCache_;

  // 预览 block（正文行）-> 源行映射（仅当前可见行，用于标记无效内容）。
  QVector<int> blockToSource_;
  QVector<int> sourceToBlock_;
  // 右键菜单可能在 Qt 清空选择之后才到达；记住最近一次非空选择，
  // 「设置为无效内容」在菜单打开时使用它。
  QTextCursor lastPreviewSelection_;
  QTextCursor pendingInvalidSelection_;

  // 撤销 / 重做
  std::vector<Snapshot> undoStack_;
  std::vector<Snapshot> redoStack_;
  static constexpr std::size_t kUndoLimit = 60;
  // 普通书（约 1000 章）仍能保留完整 60 步；大书自动少留几步，避免撤销栈
  // 无上限吃内存。单步超限时仍至少保留一步。
  static constexpr std::size_t kUndoBytesLimit = 24ull * 1024 * 1024;
};

}  // namespace ui
