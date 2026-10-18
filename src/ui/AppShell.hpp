#pragma once

#include <QAction>
#include <QCloseEvent>
#include <QListWidget>
#include <QMainWindow>
#include <QStackedWidget>

#include <filesystem>

namespace ui {

class MainWindow;
class DialoguePage;

// 桌面端外壳：左侧菜单 + 右侧堆叠子页面（章节整理 / 台词拆分）。
// 只承载窗口与导航，不实现任何 core 算法。
class AppShell : public QMainWindow {
  Q_OBJECT

 public:
  explicit AppShell(QWidget* parent = nullptr);

  // 启动时检查章节页自动备份；返回 true 表示已恢复。
  bool tryRecoverBackup();

  // 加载指定 TXT 到章节页（--input 用）。path 为空则忽略。
  void loadFile(const std::filesystem::path& path);

  // 切到台词拆分页并打开文件（供外壳统一入口复用）。
  void openDialogue(const std::filesystem::path& path);

 protected:
  void closeEvent(QCloseEvent* event) override;

 private:
  void buildUi();
  void buildMenu();
  void switchTo(int index);
  void syncNav(int index);
  void onNavActivated(int row);
  void onToggleTheme();

  MainWindow* chapterPage_ = nullptr;
  DialoguePage* dialoguePage_ = nullptr;
  QStackedWidget* stack_ = nullptr;
  QListWidget* navList_ = nullptr;
  QAction* actChapter_ = nullptr;
  QAction* actDialogue_ = nullptr;
};

}  // namespace ui
