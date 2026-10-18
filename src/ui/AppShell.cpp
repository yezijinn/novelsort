#include "ui/AppShell.hpp"

#include "ui/AppIdentity.hpp"

#include <QAction>
#include <QApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "ui/DialoguePage.hpp"
#include "ui/MainWindow.hpp"
#include "ui/Theme.hpp"
#include "ui/UiMetrics.hpp"

namespace ui {

AppShell::AppShell(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle(QStringLiteral("novelsort · 小说整理与有声书脚本"));
  resize(1440, 900);
  setMinimumSize(1320, 760);
  buildUi();
  buildMenu();
}

void AppShell::buildUi() {
  auto* central = new QWidget(this);
  setCentralWidget(central);
  auto* root = new QHBoxLayout(central);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  auto* sidebar = new QFrame(this);
  sidebar->setObjectName(QStringLiteral("appSidebar"));
  sidebar->setFixedWidth(220);
  auto* sideLayout = new QVBoxLayout(sidebar);
  sideLayout->setContentsMargins(16, 18, 16, 16);
  sideLayout->setSpacing(10);

  auto* appName = new QLabel(QStringLiteral("novelsort"), sidebar);
  appName->setObjectName(QStringLiteral("appName"));
  auto* appHint =
      new QLabel(QStringLiteral("小说整理与有声书脚本"), sidebar);
  appHint->setObjectName(QStringLiteral("appHint"));
  sideLayout->addWidget(appName);
  sideLayout->addWidget(appHint);
  sideLayout->addSpacing(14);

  navList_ = new QListWidget(sidebar);
  navList_->setObjectName(QStringLiteral("navList"));
  navList_->addItem(QStringLiteral("章节整理"));
  navList_->addItem(QStringLiteral("台词拆分"));
  navList_->setCurrentRow(0);
  connect(navList_, &QListWidget::currentRowChanged, this,
          &AppShell::onNavActivated);
  sideLayout->addWidget(navList_, 1);

  auto* themeButton = new QPushButton(QStringLiteral("明暗主题"), sidebar);
  themeButton->setObjectName(QStringLiteral("icon"));
  themeButton->setFixedHeight(metrics::kControlHeight);
  themeButton->setCursor(Qt::PointingHandCursor);
  connect(themeButton, &QPushButton::clicked, this,
          &AppShell::onToggleTheme);
  sideLayout->addWidget(themeButton);

  root->addWidget(sidebar);

  stack_ = new QStackedWidget(this);
  chapterPage_ = new MainWindow(stack_);
  chapterPage_->prepareEmbedded();
  dialoguePage_ = new DialoguePage(stack_);
  stack_->addWidget(chapterPage_);
  stack_->addWidget(dialoguePage_);
  root->addWidget(stack_, 1);
}

void AppShell::buildMenu() {
  auto* fileMenu = menuBar()->addMenu(QStringLiteral("文件"));
  auto* exitAction = new QAction(QStringLiteral("退出"), this);
  exitAction->setShortcut(QKeySequence::Quit);
  connect(exitAction, &QAction::triggered, this, &QWidget::close);
  fileMenu->addAction(exitAction);

  auto* viewMenu = menuBar()->addMenu(QStringLiteral("视图"));
  actChapter_ = new QAction(QStringLiteral("章节整理"), this);
  actChapter_->setShortcut(QKeySequence(QStringLiteral("Ctrl+1")));
  connect(actChapter_, &QAction::triggered, this, [this] { switchTo(0); });
  viewMenu->addAction(actChapter_);
  actDialogue_ = new QAction(QStringLiteral("台词拆分"), this);
  actDialogue_->setShortcut(QKeySequence(QStringLiteral("Ctrl+2")));
  connect(actDialogue_, &QAction::triggered, this, [this] { switchTo(1); });
  viewMenu->addAction(actDialogue_);

  auto* helpMenu = menuBar()->addMenu(QStringLiteral("帮助"));
  auto* aboutAction = new QAction(QStringLiteral("关于"), this);
  connect(aboutAction, &QAction::triggered, this, [this] {
    QMessageBox::about(
        this, QStringLiteral("关于 novelsort"),
        QStringLiteral("novelsort\n小说整理与有声书脚本工具"));
  });
  helpMenu->addAction(aboutAction);
}

void AppShell::switchTo(int index) {
  if (index < 0 || index > 1) return;
  stack_->setCurrentIndex(index);
  syncNav(index);
}

void AppShell::syncNav(int index) {
  if (navList_ == nullptr) return;
  if (navList_->currentRow() != index) navList_->setCurrentRow(index);
}

void AppShell::onNavActivated(int row) {
  if (row < 0 || row > 1) return;
  if (stack_->currentIndex() != row) stack_->setCurrentIndex(row);
}

void AppShell::onToggleTheme() {
  chapterPage_->toggleTheme();
#ifdef _WIN32
  ui::applyWindowsFrame(winId(), chapterPage_->isDarkTheme());
#endif
}

void AppShell::closeEvent(QCloseEvent* event) {
  chapterPage_->forwardCloseEvent(event);
}

bool AppShell::tryRecoverBackup() { return chapterPage_->tryRecoverBackup(); }

void AppShell::loadFile(const std::filesystem::path& path) {
  if (path.empty()) return;
  switchTo(0);
  chapterPage_->loadFile(path);
}

void AppShell::openDialogue(const std::filesystem::path& path) {
  switchTo(1);
  dialoguePage_->openFile(path);
}

}  // namespace ui
