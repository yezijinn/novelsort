#include "ui/MainWindow.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDataStream>
#include <QDateTime>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QModelIndex>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizeGrip>
#include <QSortFilterProxyModel>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTextBlockFormat>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextBlock>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "core/ChapterOrder.hpp"
#include "core/ChapterParser.hpp"
#include "core/DirectoryEdit.hpp"
#include "core/TextFile.hpp"
#include "ui/ChapterTableView.hpp"
#include "ui/AppIdentity.hpp"
#include "ui/ChapterDialogs.hpp"
#include "ui/CheckBoxStyle.hpp"
#include "ui/PreviewEdit.hpp"
#include "ui/RecoveryStore.hpp"
#include "ui/Theme.hpp"
#include "ui/UiMetrics.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace ui {

namespace {

// 自绘标题栏：Windows 10 原生标题栏在系统暗色下不会跟随应用亮色主题。
// 拖动用 WM_NCLBUTTONDOWN/HTCAPTION 交给系统，保留贴边分屏和双击最大化。
class TitleBar : public QFrame {
 public:
  explicit TitleBar(QWidget* parent = nullptr) : QFrame(parent) {}

 protected:
  void mousePressEvent(QMouseEvent* event) override {
    if (event->button() != Qt::LeftButton) {
      QFrame::mousePressEvent(event);
      return;
    }
    QWidget* w = window();
    if (w && !w->isMaximized()) {
      dragging_ = true;
      dragOffset_ = event->globalPos() - w->frameGeometry().topLeft();
    }
    event->accept();
  }

  void mouseMoveEvent(QMouseEvent* event) override {
    if (dragging_ && (event->buttons() & Qt::LeftButton)) {
      if (QWidget* w = window()) w->move(event->globalPos() - dragOffset_);
      event->accept();
      return;
    }
    QFrame::mouseMoveEvent(event);
  }

  void mouseReleaseEvent(QMouseEvent* event) override {
    dragging_ = false;
    QFrame::mouseReleaseEvent(event);
  }

  void mouseDoubleClickEvent(QMouseEvent* event) override {
    if (event->button() == Qt::LeftButton) {
      QWidget* w = window();
      if (w) {
        if (w->isMaximized()) {
          w->showNormal();
        } else {
          w->showMaximized();
        }
      }
      event->accept();
      return;
    }
    QFrame::mouseDoubleClickEvent(event);
  }

 private:
  bool dragging_ = false;
  QPoint dragOffset_;
};

void show_info_message(QWidget* parent, const QString& title,
                       const QString& text) {
  QMessageBox box(parent);
  box.setIcon(QMessageBox::Information);
  box.setWindowTitle(title);
  box.setText(text);
  QPushButton* ok = box.addButton(QStringLiteral("确定"),
                                  QMessageBox::AcceptRole);
  box.setDefaultButton(ok);
  box.setEscapeButton(ok);
  box.exec();
}

void show_warning_message(QWidget* parent, const QString& title,
                          const QString& text) {
  QMessageBox box(parent);
  box.setIcon(QMessageBox::Warning);
  box.setWindowTitle(title);
  box.setText(text);
  QPushButton* ok = box.addButton(QStringLiteral("确定"),
                                  QMessageBox::AcceptRole);
  box.setDefaultButton(ok);
  box.setEscapeButton(ok);
  box.exec();
}

// 长操作期间显示等待光标；restore() 可在弹错误框前提前恢复。
class ScopedWaitCursor {
 public:
  ScopedWaitCursor() { QApplication::setOverrideCursor(Qt::WaitCursor); }
  ~ScopedWaitCursor() { restore(); }
  void restore() {
    if (!active_) return;
    QApplication::restoreOverrideCursor();
    active_ = false;
  }

 private:
  bool active_ = true;
};

// string_view 转 QString（UTF-8），零拷贝，不先拷成 std::string。
QString qs(std::string_view s) {
  return QString::fromUtf8(s.data(), static_cast<int>(s.size()));
}

// 对升序的 skip_lines 计算一个稳定哈希，用于预览缓存键。
std::size_t skipLinesHash(const std::vector<std::size_t>& v) {
  std::size_t h = 1469598103934665603ull;  // FNV64 offset basis
  for (const std::size_t x : v) {
    h ^= x;
    h *= 1099511628211ull;  // FNV64 prime
  }
  return h;
}

std::size_t lineRangesHash(const std::vector<novel::LineRange>& v) {
  std::size_t h = 1469598103934665603ull;
  for (const novel::LineRange& r : v) {
    h ^= r.begin;
    h *= 1099511628211ull;
    h ^= r.end;
    h *= 1099511628211ull;
  }
  return h;
}

std::size_t exclusionHash(const std::vector<novel::TextExclusion>& v) {
  std::size_t h = 1469598103934665603ull;
  for (const novel::TextExclusion& e : v) {
    h ^= e.line;
    h *= 1099511628211ull;
    h ^= e.begin_byte;
    h *= 1099511628211ull;
    h ^= e.end_byte;
    h *= 1099511628211ull;
  }
  return h;
}

// 章节列表自定义绘制：紧凑行高 30；第一列固定 88px 序号（弱化等宽），
// 两列之间 1px 分隔线；行与行之间一条横线；标题单行省略；
// 选中行圆角强调底 + 左侧 3px 强调条；卷标题「卷」、合成前言「前」徽标。
class ChapterDelegate : public QStyledItemDelegate {
 public:
  explicit ChapterDelegate(QObject* parent = nullptr)
      : QStyledItemDelegate(parent) {}

  QSize sizeHint(const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override {
    (void)option;
    (void)index;
    return QSize(metrics::kDelegateWidth, metrics::kRowHeight);
  }

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override {
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const bool selected = option.state & QStyle::State_Selected;
    const bool hovered = option.state & QStyle::State_MouseOver;

    const QRect r = option.rect.adjusted(4, 0, -4, 0);

    if (selected || hovered) {
      QPainterPath path;
      path.addRoundedRect(r, 9, 9);
      painter->fillPath(path, selected ? C.accentBg : C.hover);
    }

    if (selected) {
      QPainterPath barPath;
      barPath.addRoundedRect(QRect(r.left() + 2, r.top() + 4, 3, r.height() - 8),
                             1.5, 1.5);
      painter->fillPath(barPath, C.accent);
    }

    const int level = index.data(RoleLevel).toInt();
    const QString numberText = index.data(RoleNumberText).toString();
    const QString titleText = index.data(RoleTitleText).toString();
    const bool synthetic = index.data(RoleSynthetic).toBool();
    const bool excluded = index.data(RoleExcluded).toBool();

    const int kNumCol = metrics::kNumberColumnWidth;
    const int numColRight = r.left() + kNumCol;
    const QRect numRect(r.left() + 12, r.top(), kNumCol - 18, r.height());
    const QRect titleRect(numColRight + 8, r.top(),
                          r.right() - numColRight - 10, r.height());

    // 第一列：序号文本（有编号）；排除编号用 warning 色
    if (!numberText.isEmpty()) {
      QFont numFont(FONT_MONO);
      numFont.setPointSize(10);
      painter->setFont(numFont);
      painter->setPen(excluded ? C.warning : (selected ? C.accent : C.sub));
      painter->drawText(numRect, Qt::AlignLeft | Qt::AlignVCenter, numberText);
    }

    // 列分隔线 1px
    painter->setPen(QPen(C.border, 1));
    painter->drawLine(numColRight, r.top() + 4, numColRight, r.bottom() - 4);

    int x = titleRect.left();
    if (level == 1) {
      const QRect badgeRect(x, titleRect.top() + (titleRect.height() - 16) / 2,
                            22, 16);
      QPainterPath badgePath;
      badgePath.addRoundedRect(badgeRect, 4, 4);
      painter->fillPath(badgePath, C.accentBg);
      painter->setPen(QPen(C.accent, 1));
      painter->drawPath(badgePath);
      QFont badgeFont(FONT_UI);
      badgeFont.setPointSize(9);
      painter->setFont(badgeFont);
      painter->drawText(badgeRect, Qt::AlignCenter, QStringLiteral("卷"));
      x = badgeRect.right() + 8;
    } else if (synthetic) {
      const QRect badgeRect(x, titleRect.top() + (titleRect.height() - 16) / 2,
                            22, 16);
      QPainterPath badgePath;
      badgePath.addRoundedRect(badgeRect, 4, 4);
      painter->fillPath(badgePath, C.hover);
      painter->setPen(QPen(C.sub, 1));
      painter->drawPath(badgePath);
      QFont badgeFont(FONT_UI);
      badgeFont.setPointSize(9);
      painter->setFont(badgeFont);
      painter->drawText(badgeRect, Qt::AlignCenter, QStringLiteral("前"));
      x = badgeRect.right() + 8;
    }

    // tooltip：排除编号时说明
    if (excluded) {
      const QString tip = QStringLiteral("不参与编号");
      QFont tipFont(FONT_UI);
      tipFont.setPointSize(9);
      const int tw = QFontMetrics(tipFont).width(tip);
      painter->setFont(tipFont);
      painter->setPen(C.warning);
      painter->drawText(QRect(x, titleRect.top(), tw, titleRect.height()),
                        Qt::AlignLeft | Qt::AlignVCenter, tip);
      x += tw + 6;
    }

    const QRect textRect(x, titleRect.top(), titleRect.right() - x + 1,
                         titleRect.height());
    QFont titleFont(FONT_UI);
    titleFont.setPointSizeF(10.5);
    if (level == 1) titleFont.setBold(true);
    painter->setFont(titleFont);
    painter->setPen(excluded && !numberText.isEmpty() ? C.warning : C.text);
    const QString elided = painter->fontMetrics().elidedText(
        titleText, Qt::ElideRight, textRect.width());
    painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, elided);

    // 行间横线：行距压到 0，用 1px 线分隔，不再依赖空白。
    painter->setPen(QPen(C.border, 1));
    painter->drawLine(r.left(), r.bottom(), r.right(), r.bottom());

    painter->restore();
  }
};

}  // namespace

struct MainWindow::Buttons {
  QPushButton* open = nullptr;
  QPushButton* undo = nullptr;
  QPushButton* redo = nullptr;
  QPushButton* renumber = nullptr;     // 重新编排章节（统一编号）
  QPushButton* resetOrder = nullptr;  // 全部恢复原样
  QPushButton* exportTxt = nullptr;    // 导出 TXT
  QPushButton* exportJson = nullptr;   // 导出目录 JSON
  QPushButton* openExportLoc = nullptr;  // 打开导出位置
};

MainWindow::~MainWindow() = default;

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  // Windows 10 原生标题栏不跟随应用亮色主题，改用自绘标题栏。
  setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
  buildUi();
  setWindowTitle(kWindowTitle);
  resize(metrics::kWindowWidth, metrics::kWindowHeight);
  setMinimumSize(metrics::kMinWindowWidth, metrics::kMinWindowHeight);

  // 启动时按 QSettings 持久化的主题（默认暗色）应用一次。
  QSettings settings = makeSettings();
  const QString saved = settings.value(QStringLiteral("theme"), QStringLiteral("dark")).toString();
  themeDark_ = (saved != QStringLiteral("light"));
  applyThemeToWindow(themeDark_);

  recoveryTimer_ = new QTimer(this);
  recoveryTimer_->setSingleShot(true);
  connect(recoveryTimer_, &QTimer::timeout, this, [this]() {
    if (!recoveryEnabled_ || !dirty_ || !isLoaded_) return;
    writeRecoveryBackup();
    if (dirty_ && isLoaded_) recoveryTimer_->start(metrics::kRecoveryRepeatMs);
  });

  applyModel();
  setEmptyState();
  updateControls();
}

void MainWindow::buildUi() {
  QWidget* central = new QWidget(this);
  setCentralWidget(central);

  QVBoxLayout* root = new QVBoxLayout(central);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  // ===== 自绘标题栏（34px）=====
  TitleBar* titleBar = new TitleBar(this);
  titleBar->setObjectName(QStringLiteral("customTitleBar"));
  titleBar->setFixedHeight(metrics::kTitleBarHeight);
  QHBoxLayout* titleLayout = new QHBoxLayout(titleBar);
  titleLayout->setContentsMargins(12, 0, 0, 0);
  titleLayout->setSpacing(0);
  QLabel* windowTitle =
      new QLabel(kWindowTitle, titleBar);
  windowTitle->setObjectName(QStringLiteral("windowTitle"));
  windowTitle->setAttribute(Qt::WA_TransparentForMouseEvents);
  titleLayout->addWidget(windowTitle);
  titleLayout->addStretch(1);

  const auto makeTitleButton = [&](const QString& text, const QString& tip,
                                   bool close) {
    QPushButton* button = new QPushButton(text, titleBar);
    button->setObjectName(QStringLiteral("titleButton"));
    button->setProperty("role", close ? QStringLiteral("close")
                                     : QStringLiteral("normal"));
    button->setFixedSize(metrics::kTitleButtonWidth, metrics::kTitleBarHeight);
    button->setCursor(Qt::PointingHandCursor);
    button->setFocusPolicy(Qt::NoFocus);
    button->setToolTip(tip);
    return button;
  };
  QPushButton* minimize = makeTitleButton(QStringLiteral("—"),
                                          QStringLiteral("最小化"), false);
  titleMax_ = makeTitleButton(QStringLiteral("□"), QStringLiteral("最大化"),
                              false);
  QPushButton* close =
      makeTitleButton(QStringLiteral("✕"), QStringLiteral("关闭"), true);
  titleLayout->addWidget(minimize);
  titleLayout->addWidget(titleMax_);
  titleLayout->addWidget(close);
  connect(minimize, &QPushButton::clicked, this, &QWidget::showMinimized);
  connect(titleMax_, &QPushButton::clicked, this, [this]() {
    if (isMaximized()) {
      showNormal();
    } else {
      showMaximized();
    }
  });
  connect(close, &QPushButton::clicked, this, &QWidget::close);
  root->addWidget(titleBar);

  // ===== 顶栏与导出工具条 =====
  QFrame* topBar = new QFrame(this);
  topBar->setFixedHeight(metrics::kTopBarHeight);
  topBar->setObjectName(QStringLiteral("topBar"));
  QHBoxLayout* topLayout = new QHBoxLayout(topBar);
  topLayout->setContentsMargins(0, 0, 0, 0);
  topLayout->setSpacing(8);

  // 左侧显示当前文件名，完整路径放 tooltip；不会挤掉右侧按钮。
  fileLabel_ = new QLabel(QStringLiteral("未打开文件"), topBar);
  fileLabel_->setObjectName(QStringLiteral("fileLabel"));
  fileLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  fileLabel_->setMinimumWidth(0);
  fileLabel_->setToolTip(QStringLiteral("当前文件"));
  topLayout->addWidget(fileLabel_, 1);

  const QString chipStyle = QStringLiteral(
      "QLabel{background:transparent;}");
  encodingChip_ = new QLabel(this);
  countChip_ = new QLabel(this);
  dirtyChip_ = new QLabel(this);
  encodingChip_->setProperty("class", "chip");
  countChip_->setProperty("class", "chip");
  dirtyChip_->setProperty("class", "chip");
  dirtyChip_->setObjectName(QStringLiteral("dirtyChip"));
  for (QLabel* chip : {encodingChip_, countChip_, dirtyChip_}) {
    chip->setFixedHeight(metrics::kControlHeight);
    chip->setAlignment(Qt::AlignCenter);
  }
  encodingChip_->hide();
  countChip_->hide();
  dirtyChip_->hide();

  // 撤销 / 重做 小按钮（文字按钮，避免缺字字形出现空方块）
  btns_ = std::make_unique<Buttons>();
  Buttons* b = btns_.get();
  b->undo = new QPushButton(QStringLiteral("撤销"));
  b->redo = new QPushButton(QStringLiteral("重做"));
  for (QPushButton* p : {b->undo, b->redo}) {
    p->setCursor(Qt::PointingHandCursor);
    p->setObjectName(QStringLiteral("icon"));
    p->setFixedHeight(metrics::kControlHeight);
    p->setMinimumWidth(metrics::kUndoMinWidth);
    p->setToolTip(QStringLiteral("撤销"));
  }
  connect(b->undo, &QPushButton::clicked, this, &MainWindow::onUndo);
  connect(b->redo, &QPushButton::clicked, this, &MainWindow::onRedo);
  b->undo->setToolTip(
      QStringLiteral("撤销上一步编辑（Ctrl+Z）；可连续撤销"));
  b->redo->setToolTip(
      QStringLiteral("重做刚才撤销的编辑（Ctrl+Y / Ctrl+Shift+Z）"));

  themeBtn_ = new QPushButton(QStringLiteral("明暗主题"));
  themeBtn_->setObjectName(QStringLiteral("icon"));
  themeBtn_->setFixedHeight(metrics::kControlHeight);
  themeBtn_->setCursor(Qt::PointingHandCursor);
  themeBtn_->setToolTip(QStringLiteral("切换明/暗主题"));
  connect(themeBtn_, &QPushButton::clicked, this, &MainWindow::toggleTheme);

  b->open = new QPushButton(QStringLiteral("打开 TXT"));
  b->open->setProperty("class", "primary");
  b->open->setCursor(Qt::PointingHandCursor);
  b->open->setFixedHeight(metrics::kControlHeight);
  connect(b->open, &QPushButton::clicked, this, &MainWindow::onOpenClicked);

  b->exportTxt = new QPushButton(QStringLiteral("导出 TXT"));
  b->exportTxt->setObjectName(QStringLiteral("icon"));
  b->exportTxt->setFixedHeight(metrics::kControlHeight);
  b->exportTxt->setCursor(Qt::PointingHandCursor);
  connect(b->exportTxt, &QPushButton::clicked, this, &MainWindow::onExportTxt);

  b->exportJson = new QPushButton(QStringLiteral("导出目录 JSON"));
  b->exportJson->setObjectName(QStringLiteral("icon"));
  b->exportJson->setFixedHeight(metrics::kControlHeight);
  b->exportJson->setCursor(Qt::PointingHandCursor);
  connect(b->exportJson, &QPushButton::clicked, this, &MainWindow::onExportJson);

  b->openExportLoc = new QPushButton(QStringLiteral("打开导出位置"));
  b->openExportLoc->setObjectName(QStringLiteral("icon"));
  b->openExportLoc->setFixedHeight(metrics::kControlHeight);
  b->openExportLoc->setCursor(Qt::PointingHandCursor);
  b->openExportLoc->setEnabled(false);  // 导出前禁用
  connect(b->openExportLoc, &QPushButton::clicked, this,
          &MainWindow::onOpenExportLocation);

  topLayout->addWidget(encodingChip_);
  topLayout->addWidget(countChip_);
  topLayout->addWidget(dirtyChip_);
  topLayout->addWidget(b->undo);
  topLayout->addWidget(b->redo);
  topLayout->addWidget(themeBtn_);
  topLayout->addWidget(b->open);
  // 导出工具条：导出按钮、目标选项、打开位置放在同一行，全部常驻可见。
  QFrame* exportBar = new QFrame(this);
  exportBar->setObjectName(QStringLiteral("exportBar"));
  exportBar->setFixedHeight(metrics::kExportBarHeight);
  QHBoxLayout* exportLayout = new QHBoxLayout(exportBar);
  exportLayout->setContentsMargins(0, 0, 0, 0);
  exportLayout->setSpacing(8);
  exportLayout->addWidget(b->exportTxt, 1);
  exportLayout->addWidget(b->exportJson, 1);

  exportToExecDir_ = new QCheckBox(this);
  exportToExecDir_->hide();
  {
    QSettings settings = makeSettings();
    exportToExecDir_->setChecked(
        settings.value(QStringLiteral("exportToExecDir"), true).toBool());
  }
  exportToExecDirButton_ =
      new QPushButton(QStringLiteral("导出到程序位置：开"), this);
  exportToExecDirButton_->setObjectName(QStringLiteral("icon"));
  exportToExecDirButton_->setCursor(Qt::PointingHandCursor);
  exportToExecDirButton_->setToolTip(
      QStringLiteral("开：直接导出到 exe 同级目录；关：每次选择保存位置"));
  connect(exportToExecDirButton_, &QPushButton::clicked, this, [this]() {
    exportToExecDir_->setChecked(!exportToExecDir_->isChecked());
  });
  connect(exportToExecDir_, &QCheckBox::toggled, this,
          [this]() { updateExportToExecDirButton(); });
  for (QPushButton* button :
       {b->exportTxt, b->exportJson, exportToExecDirButton_,
        b->openExportLoc}) {
    button->setFixedHeight(metrics::kControlHeight);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  }
  updateExportToExecDirButton();
  exportLayout->addWidget(exportToExecDirButton_, 1);
  exportLayout->addWidget(b->openExportLoc, 1);

  // ===== 主体：左侧目录占满整个高度，控制区全部放到右侧 =====
  QHBoxLayout* body = new QHBoxLayout();
  body->setContentsMargins(0, 0, 0, 0);
  body->setSpacing(0);

  // 左栏：目录操作、搜索与章节列表。
  QFrame* left = new QFrame(this);
  left->setObjectName(QStringLiteral("sidebar"));
  left->setFixedWidth(metrics::kSidebarWidth);
  left->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
  QVBoxLayout* leftL = new QVBoxLayout(left);
  leftL->setContentsMargins(12, 12, 8, 12);
  leftL->setSpacing(8);

  QHBoxLayout* listHead = new QHBoxLayout();
  QLabel* dirTitle = new QLabel(QStringLiteral("章节目录"));
  dirTitle->setObjectName(QStringLiteral("dirTitle"));
  QLabel* dirCount = new QLabel(this);
  dirCount->setObjectName(QStringLiteral("dirCount"));
  listHead->addWidget(dirTitle);
  listHead->addStretch(1);
  listHead->addWidget(dirCount);
  leftL->addLayout(listHead);

  // 目录操作行：两个等宽按钮，不再塞进标题行。
  b->renumber = new QPushButton(QStringLiteral("重新编排章节"));
  b->resetOrder = new QPushButton(QStringLiteral("全部恢复原样"));
  for (QPushButton* p : {b->renumber, b->resetOrder}) {
    p->setObjectName(QStringLiteral("icon"));
    p->setFixedHeight(metrics::kControlHeight);
    p->setCursor(Qt::PointingHandCursor);
  }
  connect(b->renumber, &QPushButton::clicked, this, &MainWindow::onRenumber);
  connect(b->resetOrder, &QPushButton::clicked, this, &MainWindow::onResetOrder);
  QHBoxLayout* dirOps = new QHBoxLayout();
  dirOps->setSpacing(8);
  dirOps->setContentsMargins(0, 0, 0, 0);
  dirOps->addWidget(b->renumber, 1);
  dirOps->addWidget(b->resetOrder, 1);
  leftL->addLayout(dirOps);

  // 无编号标题开关：用按钮文字显示开/关，状态仍由隐藏复选框保存。
  acceptUnnumbered_ = new QCheckBox(this);
  acceptUnnumbered_->hide();
  connect(acceptUnnumbered_, &QCheckBox::toggled, this,
          &MainWindow::onAcceptUnnumberedToggled);
  connect(acceptUnnumbered_, &QCheckBox::toggled, this,
          [this]() { updateAcceptUnnumberedButton(); });

  acceptUnnumberedButton_ =
      new QPushButton(QStringLiteral("识别无编号标题：关"), this);
  acceptUnnumberedButton_->setObjectName(QStringLiteral("icon"));
  acceptUnnumberedButton_->setFixedHeight(metrics::kControlHeight);
  acceptUnnumberedButton_->setCursor(Qt::PointingHandCursor);
  acceptUnnumberedButton_->setToolTip(
      QStringLiteral("识别「启程」「相遇」这类无编号短标题"));
  QPushButton* sortHintButton =
      new QPushButton(QStringLiteral("排序提示"), this);
  sortHintButton->setObjectName(QStringLiteral("icon"));
  sortHintButton->setFixedHeight(metrics::kControlHeight);
  sortHintButton->setCursor(Qt::PointingHandCursor);
  sortHintButton->setToolTip(QStringLiteral("查看鼠标拖动与键盘排序说明"));
  connect(acceptUnnumberedButton_, &QPushButton::clicked, this, [this]() {
    acceptUnnumbered_->setChecked(!acceptUnnumbered_->isChecked());
  });
  connect(sortHintButton, &QPushButton::clicked, this, [this]() {
    show_info_message(
        this, QStringLiteral("排序提示"),
        QStringLiteral(
            "鼠标操作\n"
            "• 按住章节目录中的任意章节行，上下拖动。\n"
            "• 蓝色横线表示插入位置，松开鼠标完成排序。\n"
            "• 拖动会连同该章正文一起移动，操作可撤销。\n\n"
            "键盘操作\n"
            "• Alt+↑ / Alt+↓：上移 / 下移一章\n"
            "• Ctrl+Z：撤销\n"
            "• Ctrl+Y 或 Ctrl+Shift+Z：重做\n\n"
            "排序完成后可直接导出 TXT。"));
  });

  QHBoxLayout* toggleRow = new QHBoxLayout();
  toggleRow->setSpacing(8);
  toggleRow->setContentsMargins(0, 0, 0, 0);
  toggleRow->addWidget(acceptUnnumberedButton_, 1);
  toggleRow->addWidget(sortHintButton, 1);
  leftL->addLayout(toggleRow);
  updateAcceptUnnumberedButton();

  searchEdit_ = new QLineEdit(this);
  searchEdit_->setPlaceholderText(QStringLiteral("搜索章节标题…"));
  leftL->addWidget(searchEdit_);
  connect(searchEdit_, &QLineEdit::textChanged, this,
          &MainWindow::onSearchChanged);

  tableView_ = new ChapterTableView(this);
  tableView_->setShowGrid(false);
  tableView_->verticalHeader()->setVisible(false);
  tableView_->horizontalHeader()->setVisible(false);
  tableView_->setSelectionBehavior(QAbstractItemView::SelectRows);
  tableView_->setSelectionMode(QAbstractItemView::SingleSelection);
  tableView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tableView_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  tableView_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  tableView_->setItemDelegate(new ChapterDelegate(this));
  tableView_->setCornerButtonEnabled(false);
  tableView_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  tableView_->verticalHeader()->setDefaultSectionSize(metrics::kRowHeight);
  tableView_->verticalHeader()->setMinimumSectionSize(metrics::kRowHeight);
  tableView_->setMinimumHeight(metrics::kTableMinHeight);
  tableView_->setContextMenuPolicy(Qt::CustomContextMenu);
  tableView_->setToolTip(
      QStringLiteral("按住章节行拖动排序；也可用 Alt+↑ / Alt+↓ 移动"));
  connect(tableView_, &QWidget::customContextMenuRequested, this,
          &MainWindow::onListContextMenu);
  connect(tableView_, &ChapterTableView::rowDropped, this,
          &MainWindow::onRowDropped);
  leftL->addWidget(tableView_, 1);
  leftL->setStretchFactor(tableView_, 1);

  body->addWidget(left);

  // 右栏（预览）
  QFrame* right = new QFrame(this);
  right->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  QVBoxLayout* rightL = new QVBoxLayout(right);
  rightL->setContentsMargins(12, 12, 12, 0);
  rightL->setSpacing(8);
  rightL->addWidget(topBar);
  rightL->addWidget(exportBar);

  // 预览卡片容器
  QFrame* card = new QFrame(this);
  card->setObjectName(QStringLiteral("previewCard"));
  card->setAttribute(Qt::WA_StyledBackground, true);
  card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  QVBoxLayout* cardL = new QVBoxLayout(card);
  cardL->setContentsMargins(16, 14, 16, 14);
  cardL->setSpacing(0);

  // 标题 + 元信息（头部区域）
  QWidget* head = new QWidget(card);
  previewHead_ = head;
  QVBoxLayout* headL = new QVBoxLayout(head);
  headL->setContentsMargins(0, 0, 0, 0);
  headL->setSpacing(6);

  QHBoxLayout* titleRow = new QHBoxLayout();
  titleRow->setContentsMargins(0, 0, 0, 0);
  titleRow->setSpacing(12);
  previewTitle_ = new QLabel(head);
  previewTitle_->setObjectName(QStringLiteral("previewTitle"));
  previewMeta_ = new QLabel(head);
  previewMeta_->setObjectName(QStringLiteral("previewMeta"));
  titleRow->addWidget(previewTitle_, 0, Qt::AlignBaseline);
  titleRow->addStretch(1);
  titleRow->addWidget(previewMeta_, 0, Qt::AlignBaseline);
  headL->addLayout(titleRow);

  QFrame* headRule = new QFrame(head);
  headRule->setObjectName(QStringLiteral("rule"));
  headRule->setFixedHeight(metrics::kHairline);
  headL->addWidget(headRule);
  cardL->addWidget(head);

  previewWarningBar_ = new QFrame(card);
  previewWarningBar_->setObjectName(QStringLiteral("previewWarningBar"));
  QHBoxLayout* warningLayout = new QHBoxLayout(previewWarningBar_);
  warningLayout->setContentsMargins(8, 6, 8, 6);
  warningLayout->setSpacing(8);
  previewWarning_ = new QLabel(previewWarningBar_);
  previewWarning_->setObjectName(QStringLiteral("previewWarning"));
  previewWarning_->setWordWrap(true);
  previewWarning_->installEventFilter(this);
  previewWarning_->setCursor(Qt::PointingHandCursor);
  warningLayout->addWidget(previewWarning_, 1);
  QPushButton* closeWarning =
      new QPushButton(QStringLiteral("关闭这条提醒"), previewWarningBar_);
  closeWarning->setObjectName(QStringLiteral("warningClose"));
  closeWarning->setCursor(Qt::PointingHandCursor);
  closeWarning->setFocusPolicy(Qt::NoFocus);
  closeWarning->setFixedHeight(metrics::kWarningCloseHeight);
  closeWarning->setToolTip(QStringLiteral("隐藏这条提醒，不改变文件内容"));
  warningLayout->addWidget(closeWarning, 0, Qt::AlignTop);
  connect(closeWarning, &QPushButton::clicked, this, [this]() {
    warningBannerDismissed_ = true;
    if (previewWarningBar_) previewWarningBar_->hide();
  });
  previewWarningBar_->hide();
  cardL->addWidget(previewWarningBar_);
  cardL->addSpacing(4);

  previewEdit_ = new PreviewEdit(card);
  previewEdit_->setLineWrapMode(QTextEdit::WidgetWidth);
  // 预览区右键菜单：在此拆分 / 设置为无效内容
  previewEdit_->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(previewEdit_, &QWidget::customContextMenuRequested, this,
          &MainWindow::onPreviewContextMenu);
  connect(previewEdit_, &QTextEdit::selectionChanged, this, [this]() {
    const QTextCursor cursor = previewEdit_->textCursor();
    if (cursor.hasSelection()) lastPreviewSelection_ = cursor;
  });
  cardL->addWidget(previewEdit_, 1);

  // 空状态覆盖层
  emptyLabel_ = new QLabel(QStringLiteral("打开一本 TXT，开始整理章节"), card);
  emptyLabel_->setAlignment(Qt::AlignCenter);
  emptyLabel_->setObjectName(QStringLiteral("emptyLabel"));
  emptyLabel_->hide();
  cardL->addWidget(emptyLabel_, 1);

  rightL->addWidget(card, 1);
  body->addWidget(right, 1);
  root->addLayout(body, 1);

  // ===== 底栏 =====
  QStatusBar* bar = new QStatusBar(this);
  statusLabel_ = new QLabel(QStringLiteral("未打开文件"));
  statusLabel_->setObjectName(QStringLiteral("statusLabel"));
  bar->addWidget(statusLabel_, 1);

  metaChip_ = new QLabel(this);
  tocChip_ = new QLabel(this);
  warnChip_ = new QLabel(this);
  metaChip_->setProperty("class", "chip");
  tocChip_->setProperty("class", "chip");
  warnChip_->setProperty("class", "chip");
  warnChip_->setObjectName(QStringLiteral("warnChip"));
  warnChip_->installEventFilter(this);
  metaChip_->hide();
  tocChip_->hide();
  warnChip_->hide();
  bar->addPermanentWidget(metaChip_);
  bar->addPermanentWidget(tocChip_);
  bar->addPermanentWidget(warnChip_);
  bar->setSizeGripEnabled(false);
  rightL->addWidget(bar);

  // 快捷键
  QAction* openAct = new QAction(this);
  openAct->setShortcut(QKeySequence::Open);
  connect(openAct, &QAction::triggered, this, &MainWindow::onOpenClicked);
  addAction(openAct);

  QAction* saveAct = new QAction(this);
  saveAct->setShortcut(QKeySequence::Save);
  connect(saveAct, &QAction::triggered, this, &MainWindow::onExportTxt);
  addAction(saveAct);

  QAction* upAct = new QAction(this);
  upAct->setShortcut(QKeySequence(QStringLiteral("Alt+Up")));
  connect(upAct, &QAction::triggered, this, &MainWindow::onMoveUp);
  addAction(upAct);

  QAction* downAct = new QAction(this);
  downAct->setShortcut(QKeySequence(QStringLiteral("Alt+Down")));
  connect(downAct, &QAction::triggered, this, &MainWindow::onMoveDown);
  addAction(downAct);

  QAction* f2Act = new QAction(this);
  f2Act->setShortcut(QKeySequence(QStringLiteral("F2")));
  connect(f2Act, &QAction::triggered, this, &MainWindow::onRenameChapter);
  addAction(f2Act);

  QAction* delAct = new QAction(this);
  delAct->setShortcut(QKeySequence::Delete);
  connect(delAct, &QAction::triggered, this, &MainWindow::onDeleteChapter);
  addAction(delAct);

  QAction* undoAct = new QAction(this);
  undoAct->setShortcut(QKeySequence::Undo);
  connect(undoAct, &QAction::triggered, this, &MainWindow::onUndo);
  addAction(undoAct);

  QAction* redoAct = new QAction(this);
  redoAct->setShortcut(QKeySequence::Redo);
  connect(redoAct, &QAction::triggered, this, &MainWindow::onRedo);
  addAction(redoAct);

  QAction* redoShiftAct = new QAction(this);
  redoShiftAct->setShortcut(
      QKeySequence(QStringLiteral("Ctrl+Shift+Z")));
  connect(redoShiftAct, &QAction::triggered, this, &MainWindow::onRedo);
  addAction(redoShiftAct);

  sizeGrip_ = new QSizeGrip(central);
  sizeGrip_->setFixedSize(metrics::kSizeGrip, metrics::kSizeGrip);
  sizeGrip_->setToolTip(QStringLiteral("拖拽调整窗口大小"));
  sizeGrip_->raise();
  sizeGrip_->move(central->width() - metrics::kSizeGrip,
                  central->height() - metrics::kSizeGrip);

  setAcceptDrops(true);
}

void MainWindow::showEvent(QShowEvent* event) {
  QMainWindow::showEvent(event);
  updateTitleBarButtons();
}

void MainWindow::changeEvent(QEvent* event) {
  QMainWindow::changeEvent(event);
  if (event->type() == QEvent::WindowStateChange) updateTitleBarButtons();
}

void MainWindow::updateTitleBarButtons() {
  if (!titleMax_) return;
  titleMax_->setText(isMaximized() ? QStringLiteral("❐")
                                   : QStringLiteral("□"));
  titleMax_->setToolTip(isMaximized() ? QStringLiteral("还原")
                                      : QStringLiteral("最大化"));
}

void MainWindow::resizeEvent(QResizeEvent* event) {
  QMainWindow::resizeEvent(event);
  if (sizeGrip_ && centralWidget()) {
    sizeGrip_->move(centralWidget()->width() - sizeGrip_->width(),
                    centralWidget()->height() - sizeGrip_->height());
    sizeGrip_->raise();
  }
}

void MainWindow::applyThemeToWindow(bool dark) {
  themeDark_ = dark;
  ui::applyTheme(*qApp, dark);
  // 顶栏按钮固定文案「明暗主题」，点击在明/暗之间切换。
#ifdef _WIN32
  ui::applyWindowsFrame(winId(), dark);
  SetWindowPos(reinterpret_cast<HWND>(winId()), nullptr, 0, 0, 0, 0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                   SWP_FRAMECHANGED);
#endif
  // 刷新 delegate（viewport update）+ 重置预览缓存并重绘当前章。
  tableView_->viewport()->update();
  tableView_->style()->unpolish(tableView_);
  tableView_->style()->polish(tableView_);
  previewCache_.reset();
  const QModelIndex cur = tableView_->currentIndex();
  if (cur.isValid()) {
    previewSourceRow(proxy_->mapToSource(cur).row());
  } else {
    previewEdit_->setStyleSheet(QString());  // 重新应用全局 QSS
    previewEdit_->refreshLineHighlight();
  }
  // 对话框（改名 / 重编号）下次弹出也按当前主题；这里仅确保本窗口重绘。
  this->style()->unpolish(this);
  this->style()->polish(this);
  update();
}

void MainWindow::toggleTheme() {
  const bool next = !themeDark_;
  applyThemeToWindow(next);
  QSettings settings = makeSettings();
  settings.setValue(QStringLiteral("theme"), next ? QStringLiteral("dark")
                                                  : QStringLiteral("light"));
}

void MainWindow::applyModel() {
  chapterModel_ = new ChapterModel(this);
  proxy_ = new QSortFilterProxyModel(this);
  proxy_->setSourceModel(chapterModel_);
  proxy_->setFilterCaseSensitivity(Qt::CaseInsensitive);
  proxy_->setFilterKeyColumn(0);
  tableView_->setModel(proxy_);
  tableView_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);

  connect(tableView_->selectionModel(), &QItemSelectionModel::currentChanged,
          this, &MainWindow::onSelectionChanged);
}

void MainWindow::setEmptyState() {
  previewHead_->setVisible(false);
  previewEdit_->clear();
  previewEdit_->setVisible(false);
  if (previewWarningBar_) previewWarningBar_->hide();
  emptyLabel_->setVisible(true);
  previewLineToSource_.clear();
  blockToSource_.clear();
  sourceToBlock_.clear();
  // 文档已清空，缓存失效：下次选中必须重建，避免复用已清空的 body。
  previewCache_.reset();
}

int MainWindow::currentSourceRow() const {
  const QModelIndex cur = tableView_->currentIndex();
  if (!cur.isValid()) return -1;
  const int src = proxy_->mapToSource(cur).row();
  const int n = static_cast<int>(chapterModel_->chapters().size());
  if (src < 0 || src >= n) return -1;
  return src;
}

void MainWindow::loadFile(const std::filesystem::path& path, bool silent) {
  novel::TextLoadResult loaded = novel::load_text_file(path);
  if (!loaded.ok) {
    isLoaded_ = false;
    dirty_ = false;
    currentFile_.clear();
    if (fileLabel_) {
      fileLabel_->setText(QStringLiteral("未打开文件"));
      fileLabel_->setToolTip(QStringLiteral("当前文件"));
    }
    loadedText_.clear();
    loadedEncoding_.clear();
    lines_.clear();
    tocLines_.clear();
    warnings_.clear();
    encodingWarnings_.clear();
    lossyLines_.clear();
    lossyJumpIndex_ = 0;
    warningBannerDismissed_ = false;
    initialChapters_.clear();
    excludedRanges_.clear();
    textExclusions_.clear();
    undoStack_.clear();
    redoStack_.clear();
    if (recoveryEnabled_ && !recovering_) clearRecoveryBackup();
    chapterModel_->clear();
    searchEdit_->clear();
    setEmptyState();
    updateChips();
    updateControls();
    updateStatusBar();
    if (!silent) showLoadError(loaded.error);
    return;
  }

  loadedText_ = std::move(loaded.text);
  loadedEncoding_ = loaded.encoding;
  encodingWarnings_ = loaded.warnings;
  warnings_ = encodingWarnings_;
  warningBannerDismissed_ = false;
  rebuildLossyLines();
  currentFile_ = path;
  if (fileLabel_) {
    const QString displayPath = QString::fromStdString(novel::path_to_utf8(path));
    fileLabel_->setText(QFileInfo(displayPath).fileName());
    fileLabel_->setToolTip(QDir::toNativeSeparators(displayPath));
  }
  // 关键：先确定 loadedText_（之后不再修改/重新分配它），再切分视图。
  // 这样 lines_ 中的 string_view 在本次文件会话期间始终有效，不会悬垂。
  lines_ = novel::split_lines_view(loadedText_);
  previewCache_.reset();

  novel::ParseOptions opts;
  opts.accept_unnumbered_titles = acceptUnnumbered_->isChecked();
  const novel::ParseResult parsed = novel::parse_chapters(loadedText_, opts);
  tocLines_ = parsed.toc_lines;
  initialChapters_ = parsed.chapters;
  warnings_.insert(warnings_.end(), parsed.warnings.begin(),
                   parsed.warnings.end());

  isLoaded_ = true;
  dirty_ = false;
  excludedRanges_.clear();
  textExclusions_.clear();
  undoStack_.clear();
  redoStack_.clear();
  if (recoveryEnabled_ && !recovering_) clearRecoveryBackup();
  chapterModel_->resetChapters(parsed.chapters);

  if (proxy_->rowCount() > 0) {
    tableView_->setCurrentIndex(proxy_->index(0, 0));
    previewSourceRow(0);
  } else {
    setEmptyState();
  }

  updateChips();
  updateControls();
  updateStatusBar();
}

bool MainWindow::confirmDiscardChanges() {
  if (!isLoaded_ || !dirty_) return true;

  QMessageBox box(this);
  box.setIcon(QMessageBox::Question);
  box.setWindowTitle(QStringLiteral("尚未导出"));
  box.setText(QStringLiteral("当前内容已修改但未导出。\n继续操作前要先导出吗？"));
  QPushButton* exportButton =
      box.addButton(QStringLiteral("导出后继续"), QMessageBox::AcceptRole);
  QPushButton* discardButton =
      box.addButton(QStringLiteral("放弃修改"), QMessageBox::DestructiveRole);
  QPushButton* cancelButton =
      box.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
  box.setDefaultButton(exportButton);
  box.exec();

  if (box.clickedButton() == exportButton) return exportContent(false);
  if (box.clickedButton() == discardButton) return true;
  (void)cancelButton;
  return false;
}

void MainWindow::onOpenClicked() {
  const QString path = QFileDialog::getOpenFileName(
      this, QStringLiteral("打开 TXT 文件"),
      QCoreApplication::applicationDirPath(),
      QStringLiteral("文本文件 (*.txt);;所有文件 (*.*)"));
  if (path.isEmpty()) return;
  if (!confirmDiscardChanges()) return;

  statusLabel_->setText(QStringLiteral("正在打开：") + path);
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  ScopedWaitCursor wait;
  loadFile(novel::path_from_utf8(path.toStdString()));
}

void MainWindow::previewSourceRow(int sourceRow) {
  const std::vector<novel::Chapter>& chs = chapterModel_->chapters();
  if (sourceRow < 0 || sourceRow >= static_cast<int>(chs.size())) {
    setEmptyState();
    return;
  }
  const novel::Chapter& ch = chs[static_cast<std::size_t>(sourceRow)];
  lastPreviewSelection_ = QTextCursor();
  pendingInvalidSelection_ = QTextCursor();

  const QString kind =
      (ch.level == 1) ? QStringLiteral("卷") : QStringLiteral("CH");
  const QString meta = kind + QStringLiteral(" · 第 ") +
                       QString::number(ch.start_line + 1) +
                       QStringLiteral("-") + QString::number(ch.end_line) +
                       QStringLiteral(" 行");

  // 预览缓存键：仅当键变化（切换章节 / 拆分合并删除 / 撤销重做 / 新增无效片段
  // / 切换主题）才重建文档；键未变（仅改名 / 改层级 / 重编号）只刷新标题与元信息，
  // 复用已渲染文档。
  const std::size_t skipHash = skipLinesHash(ch.skip_lines);
  const std::size_t excludedHash = lineRangesHash(excludedRanges_);
  const std::size_t exclHash = exclusionHash(textExclusions_);
  const std::size_t extraHash = lineRangesHash(ch.extra_body);
  if (previewCache_.matches(sourceRow, ch.start_line, ch.end_line,
                             excludedHash, skipHash, exclHash, extraHash,
                             themeDark_)) {
    previewTitle_->setText(qs(ch.title));
    previewHead_->setVisible(true);
    previewMeta_->setText(meta);
    emptyLabel_->setVisible(false);
    previewEdit_->setVisible(true);
    previewLineToSource_ = previewCache_.previewLineToSource;
    blockToSource_ = previewCache_.previewLineToSource;
    // 主题切换/缓存命中后重新叠加删除线，保证视觉状态不丢。
    applyExclusionFormats(sourceToBlock_);
    previewEdit_->refreshLineHighlight();
    return;
  }

  previewTitle_->setText(qs(ch.title));
  previewHead_->setVisible(true);
  previewMeta_->setText(meta);
  emptyLabel_->setVisible(false);
  previewEdit_->setVisible(true);

  std::vector<bool> toc(lines_.size(), false);
  for (const std::size_t ln : tocLines_) {
    if (ln < toc.size()) toc[ln] = true;
  }

  // 收集要显示的源行（跳过标题/toc/skip_lines/已删除区间与首尾空白行），
  // 与映射一一对应。先收 extra_body（划入本章的前置正文），再收主正文。
  QVector<int> disp;
  {
    // 删除区间可能很多且无序：先排序合并，再按行二分查找，
    // 避免「每行 × 每个区间」的线性扫描。
    std::vector<novel::LineRange> excludedIndex = excludedRanges_;
    std::sort(excludedIndex.begin(), excludedIndex.end(),
              [](const novel::LineRange& a, const novel::LineRange& b) {
                if (a.begin != b.begin) return a.begin < b.begin;
                return a.end < b.end;
              });
    std::vector<novel::LineRange> mergedExcluded;
    mergedExcluded.reserve(excludedIndex.size());
    for (const novel::LineRange& range : excludedIndex) {
      if (range.end <= range.begin) continue;
      if (!mergedExcluded.empty() &&
          range.begin <= mergedExcluded.back().end) {
        mergedExcluded.back().end =
            std::max(mergedExcluded.back().end, range.end);
      } else {
        mergedExcluded.push_back(range);
      }
    }
    excludedIndex.swap(mergedExcluded);

    const auto excludedLine = [&excludedIndex](std::size_t ln) {
      const auto it = std::upper_bound(
          excludedIndex.begin(), excludedIndex.end(), ln,
          [](std::size_t value, const novel::LineRange& range) {
            return value < range.begin;
          });
      if (it == excludedIndex.begin()) return false;
      const auto candidate = std::prev(it);
      return ln < candidate->end;
    };
    const auto pushLine = [&](std::size_t ln) {
      if (ln >= lines_.size()) return;
      if (toc[ln]) return;
      // core 保证 ch.skip_lines 升序且无重复；UI 侧按「升序假设」二分查找，
      // 若将来 core 未保证升序需退回线性遍历。
      if (std::binary_search(ch.skip_lines.begin(), ch.skip_lines.end(), ln)) {
        return;
      }
      if (excludedLine(ln)) return;
      disp.append(static_cast<int>(ln));
    };
    for (const novel::LineRange& range : ch.extra_body) {
      const std::size_t rangeEnd = std::min(range.end, lines_.size());
      for (std::size_t ln = range.begin; ln < rangeEnd; ++ln) pushLine(ln);
    }
    const std::size_t begin = std::min(ch.start_line, lines_.size());
    const std::size_t end = std::min(ch.end_line, lines_.size());
    for (std::size_t ln = begin; ln < end; ++ln) {
      if (ln == begin && !ch.synthetic &&
          lines_[ln] == std::string_view(ch.heading_line)) {
        continue;
      }
      if (ch.suppressed && ln == begin && !ch.synthetic) continue;
      pushLine(ln);
    }
    auto isBlank = [this](int ln) {
      return qs(lines_[static_cast<std::size_t>(ln)]).trimmed().isEmpty();
    };
    while (!disp.isEmpty() && isBlank(disp.first())) disp.removeFirst();
    while (!disp.isEmpty() && isBlank(disp.last())) disp.removeLast();
  }

  // 先把要显示的行拼成一段 QString（逐行以 \n 连接，末尾不加），
  // 再一次性写入文档；最后用 mergeBlockFormat / mergeCharFormat 套用
  // 正文行高、颜色与字体，避免逐行 insertBlock + insertText 的巨额开销。
  const int kMax = metrics::kPreviewCharLimit;
  QString body;
  QVector<int> mapping;
  bool cut = false;
  {
    // 正文最多 kMax 个字符；不能按「行数 × 50」无上限预留，
    // 否则「全文」章或大量短行会白占约 10MB 临时内存。
    const std::size_t reserveChars = std::min<std::size_t>(
        static_cast<std::size_t>(disp.size()) *
                metrics::kReserveCharsPerLine +
            16,
        static_cast<std::size_t>(kMax) + 16);
    body.reserve(static_cast<int>(reserveChars));
    mapping.reserve(disp.size());
    for (int i = 0; i < disp.size(); ++i) {
      const QString line = qs(lines_[static_cast<std::size_t>(disp.at(i))]);
      // 整行截断：加入该行（含分隔符）会超限则停止，绝不在行中间截断。
      if (i == 0) {
        if (line.size() > kMax) { cut = true; break; }
        body = line;
        mapping.append(disp.at(i));
      } else {
        if (body.size() + 1 + line.size() > kMax) { cut = true; break; }
        body += QChar('\n');
        body += line;
        mapping.append(disp.at(i));
      }
    }
  }
  previewLineToSource_ = mapping;

  // 仅当前可见的源行参与「无效内容」标记；构建 source -> block 映射。
  // 行号在章节内唯一，故用 QVector<int>（下标=block，值=源行）即可。
  sourceToBlock_ = QVector<int>(static_cast<int>(lines_.size()), -1);
  {
    QTextDocument* doc = previewEdit_->document();
    doc->clear();
    doc->setDocumentMargin(0);
    QTextCursor cur(doc);

    if (body.isEmpty() && !cut) {
      // 空正文（首尾空行裁剪后无内容）：显示占位，映射为空。
      cur.insertText(QStringLiteral("（无正文内容）"));
    } else {
      if (!body.isEmpty()) {
        doc->setPlainText(body);
        QTextCursor all(doc);
        all.select(QTextCursor::Document);
        QTextCharFormat bodyFmt;
        QFont bodyFont(FONT_UI);
        bodyFont.setPointSize(12);
        bodyFmt.setFont(bodyFont);
        bodyFmt.setForeground(C.text);  // #E8ECF3
        QTextBlockFormat bodyBlock;
        bodyBlock.setLineHeight(180, QTextBlockFormat::ProportionalHeight);  // 1.8 行高
        all.mergeBlockFormat(bodyBlock);
        all.mergeCharFormat(bodyFmt);
      }
      if (cut) {
        // 截断提示作为末尾额外 block 追加（小字、次要色、上边距 14），不加入映射。
        QTextCursor tailCur(doc);
        tailCur.movePosition(QTextCursor::End);
        QTextBlockFormat tailGap;
        tailGap.setTopMargin(14);
        tailCur.insertBlock(tailGap);
        QTextCharFormat noteFmt;
        QFont noteFont(FONT_UI);
        noteFont.setPointSize(10);
        noteFmt.setFont(noteFont);
        noteFmt.setForeground(C.sub);
        tailCur.setCharFormat(noteFmt);
        tailCur.insertText(
            QStringLiteral("…… 正文过长（超过 5 万字），已截断显示。"));
      }
    }

    // 建立 block -> 源行映射（含截断提示 block 的映射为 -1）。
    blockToSource_.clear();
    blockToSource_.reserve(mapping.size() + (cut ? 1 : 0));
    QTextBlock blk = doc->firstBlock();
    int bi = 0;
    for (; blk.isValid(); blk = blk.next()) {
      const int src = (bi < mapping.size()) ? mapping.at(bi) : -1;
      blockToSource_.append(src);
      if (src >= 0 && src < sourceToBlock_.size()) sourceToBlock_[src] = bi;
      ++bi;
    }
  }

  // 在已渲染预览上叠加「无效内容」删除线样式（仍保留在文档中，可继续选中）。
  applyExclusionFormats(sourceToBlock_);

  previewEdit_->moveCursor(QTextCursor::Start);
  previewEdit_->refreshLineHighlight();

  // 更新最近一条预览缓存（只保留最近一条，不缓存所有章节）。
  previewCache_.sourceRow = sourceRow;
  previewCache_.startLine = ch.start_line;
  previewCache_.endLine = ch.end_line;
  previewCache_.excludedHash = excludedHash;
  previewCache_.skipHash = skipHash;
  previewCache_.exclusionHash = exclHash;
  previewCache_.extraHash = extraHash;
  previewCache_.themeDark = themeDark_;
  previewCache_.body = body;
  previewCache_.previewLineToSource = previewLineToSource_;
}

int MainWindow::previewCursorToSource(QTextCursor cursor) const {
  const int block = cursor.blockNumber();
  if (block < 0 || block >= blockToSource_.size()) return -1;
  return blockToSource_.at(block);
}

void MainWindow::onSelectionChanged(const QModelIndex& current,
                                     const QModelIndex& previous) {
  (void)previous;
  if (!current.isValid()) return;
  previewSourceRow(proxy_->mapToSource(current).row());
  updateControls();
}

void MainWindow::onSearchChanged(const QString& text) {
  proxy_->setFilterFixedString(text);
  // 搜索时禁用拖拽排序
  tableView_->setRowDragEnabled(text.trimmed().isEmpty());
  if (proxy_->rowCount() > 0) {
    tableView_->setCurrentIndex(proxy_->index(0, 0));
  }
}

void MainWindow::updateAcceptUnnumberedButton() {
  if (!acceptUnnumberedButton_) return;
  const bool on = acceptUnnumbered_ && acceptUnnumbered_->isChecked();
  acceptUnnumberedButton_->setText(
      on ? QStringLiteral("识别无编号标题：开")
         : QStringLiteral("识别无编号标题：关"));
}

void MainWindow::updateExportToExecDirButton() {
  if (!exportToExecDirButton_) return;
  const bool on = exportToExecDir_ && exportToExecDir_->isChecked();
  exportToExecDirButton_->setText(
      on ? QStringLiteral("导出到程序位置：开")
         : QStringLiteral("导出到程序位置：关"));
}

void MainWindow::updateControls() {
  const bool loaded = isLoaded_;
  const int n = static_cast<int>(chapterModel_->chapters().size());

  searchEdit_->setEnabled(loaded);
  // 目录操作按钮 + 识别无编号标题：仅在已加载时可用。
  if (btns_) {
    btns_->renumber->setEnabled(loaded);
    btns_->resetOrder->setEnabled(loaded);
    acceptUnnumbered_->setEnabled(loaded);
    if (acceptUnnumberedButton_) acceptUnnumberedButton_->setEnabled(loaded);
    btns_->exportTxt->setEnabled(loaded);
    btns_->exportJson->setEnabled(loaded);
  }

  // 撤销 / 重做 按钮禁用态
  btns_->undo->setEnabled(!undoStack_.empty());
  btns_->redo->setEnabled(!redoStack_.empty());

  QLabel* dirCount = findChild<QLabel*>(QStringLiteral("dirCount"));
  if (dirCount) {
    dirCount->setText(loaded ? QStringLiteral("[") + QString::number(n) +
                                   QStringLiteral("]")
                             : QString());
  }
}

void MainWindow::updateChips() {
  if (isLoaded_) {
    encodingChip_->setText(QStringLiteral("编码 ") +
                           QString::fromStdString(loadedEncoding_));
    encodingChip_->show();
    countChip_->setText(QString::number(chapterModel_->chapters().size()) +
                        QStringLiteral(" 章"));
    countChip_->show();
  } else {
    encodingChip_->hide();
    countChip_->hide();
  }
  if (dirty_ && isLoaded_) {
    dirtyChip_->setText(QStringLiteral("● 已修改"));
    dirtyChip_->show();
  } else {
    dirtyChip_->setText(QStringLiteral(""));
    dirtyChip_->hide();
  }
}

void MainWindow::updateStatusBar() {
  if (isLoaded_) {
    statusLabel_->setText(QString::fromStdString(novel::path_to_utf8(currentFile_)));

    metaChip_->setText(QStringLiteral("编码 ") +
                       QString::fromStdString(loadedEncoding_));
    metaChip_->show();
    tocChip_->setText(QStringLiteral("目录区 ") +
                      QString::number(tocLines_.size()) +
                      QStringLiteral(" 行"));
    tocChip_->show();

    if (!warnings_.empty()) {
      if (!lossyLines_.empty()) {
        warnChip_->setText(QStringLiteral("坏字节 ") +
                           QString::number(lossyLines_.size()) +
                           QStringLiteral(" 处"));
      } else {
        warnChip_->setText(QStringLiteral("提示 ") +
                           QString::number(warnings_.size()) +
                           QStringLiteral(" 条"));
      }
      warnChip_->setCursor(Qt::PointingHandCursor);
      warnChip_->show();
      QString tip;
      for (const auto& w : warnings_) {
        tip += QString::fromStdString(w) + QChar('\n');
      }
      tip += lossyLines_.empty() ? QStringLiteral("点击查看提示详情")
                                 : QStringLiteral("点击定位坏字节");
      warnChip_->setToolTip(tip);
    } else {
      warnChip_->hide();
      warnChip_->setToolTip(QString());
      warnChip_->unsetCursor();
    }
    if (dirty_) {
      statusLabel_->setText(statusLabel_->text() +
                           QStringLiteral("　● 已修改（未导出）"));
    }
  } else {
    statusLabel_->setText(QStringLiteral("未打开文件"));
    metaChip_->hide();
    tocChip_->hide();
    warnChip_->hide();
  }
  updatePreviewWarning();
}

void MainWindow::updatePreviewWarning() {
  if (!previewWarning_ || !previewWarningBar_) return;
  if (!isLoaded_ || warnings_.empty() || warningBannerDismissed_) {
    previewWarningBar_->hide();
    previewWarning_->clear();
    previewWarning_->setToolTip(QString());
    return;
  }

  QString full;
  for (const std::string& warning : warnings_) {
    full += QString::fromStdString(warning) + QChar('\n');
  }
  const int shown = std::min<int>(3, static_cast<int>(warnings_.size()));
  QString brief;
  for (int i = 0; i < shown; ++i) {
    if (i != 0) brief += QChar('\n');
    brief += QStringLiteral("· ") +
             QString::fromStdString(warnings_[static_cast<std::size_t>(i)]);
  }
  if (static_cast<int>(warnings_.size()) > shown) {
    brief += QStringLiteral("\n… 另有 ") +
             QString::number(static_cast<int>(warnings_.size()) - shown) +
             QStringLiteral(" 条，详见状态栏提示");
  }
  if (!lossyLines_.empty()) {
    brief += QStringLiteral("\n点击定位坏字节（共 ") +
             QString::number(lossyLines_.size()) + QStringLiteral(" 处）");
  } else {
    brief += QStringLiteral("\n点击查看提示详情");
  }
  previewWarning_->setText(brief);
  previewWarning_->setToolTip(full.trimmed());
  previewWarning_->setCursor(Qt::PointingHandCursor);
  previewWarningBar_->show();
}

void MainWindow::rebuildLossyLines() {
  lossyLines_.clear();
  lossyJumpIndex_ = 0;
  if (loadedEncoding_.find("容错") == std::string::npos) return;

  std::size_t line = 0;
  for (std::size_t i = 0; i + 2 < loadedText_.size(); ++i) {
    const auto c0 = static_cast<unsigned char>(loadedText_[i]);
    if (c0 == 0xEF) {
      const auto c1 = static_cast<unsigned char>(loadedText_[i + 1]);
      const auto c2 = static_cast<unsigned char>(loadedText_[i + 2]);
      if (c1 == 0xBF && c2 == 0xBD) {
        if (lossyLines_.empty() || lossyLines_.back() != line) {
          lossyLines_.push_back(line);
        }
        i += 2;
        continue;
      }
    }
    if (loadedText_[i] == '\n') ++line;
  }
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if ((watched == previewWarning_ || watched == warnChip_) &&
      event->type() == QEvent::MouseButtonRelease) {
    const auto* mouse = static_cast<QMouseEvent*>(event);
    if (mouse->button() == Qt::LeftButton) {
      onWarningClicked();
      return true;
    }
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onWarningClicked() {
  if (!lossyLines_.empty()) {
    jumpToNextLossy();
    return;
  }
  if (warnings_.empty()) return;
  QString text;
  for (const std::string& warning : warnings_) {
    text += QString::fromStdString(warning) + QChar('\n');
  }
  show_info_message(this, QStringLiteral("提示"), text.trimmed());
}

void MainWindow::jumpToNextLossy() {
  if (lossyLines_.empty()) return;
  const std::size_t index = lossyJumpIndex_ % lossyLines_.size();
  const std::size_t line = lossyLines_[index];
  ++lossyJumpIndex_;

  const std::vector<novel::Chapter>& chs = chapterModel_->chapters();
  int chapterRow = -1;
  for (int i = 0; i < static_cast<int>(chs.size()); ++i) {
    const novel::Chapter& ch = chs[static_cast<std::size_t>(i)];
    bool contains = line >= ch.start_line && line < ch.end_line;
    if (!contains) {
      for (const novel::LineRange& range : ch.extra_body) {
        if (line >= range.begin && line < range.end) {
          contains = true;
          break;
        }
      }
    }
    if (contains) {
      chapterRow = i;
      break;
    }
  }
  if (chapterRow < 0) {
    statusLabel_->setText(QStringLiteral("未能定位坏字节所在章节"));
    return;
  }

  QModelIndex proxyIndex =
      proxy_->mapFromSource(chapterModel_->index(chapterRow, 0));
  if (!proxyIndex.isValid() && !searchEdit_->text().isEmpty()) {
    searchEdit_->clear();
    proxyIndex = proxy_->mapFromSource(chapterModel_->index(chapterRow, 0));
  }
  if (proxyIndex.isValid()) {
    tableView_->setCurrentIndex(proxyIndex);
    tableView_->scrollTo(proxyIndex);
  }
  previewSourceRow(chapterRow);

  int blockIndex = -1;
  for (int i = 0; i < blockToSource_.size(); ++i) {
    if (blockToSource_[i] == static_cast<int>(line)) {
      blockIndex = i;
      break;
    }
  }

  if (blockIndex >= 0) {
    QTextDocument* doc = previewEdit_->document();
    const QTextBlock block = doc->findBlockByNumber(blockIndex);
    if (block.isValid()) {
      QTextCursor cursor(doc);
      cursor.setPosition(block.position());
      previewEdit_->setTextCursor(cursor);
      previewEdit_->ensureCursorVisible();
    }
    statusLabel_->setText(QStringLiteral("已定位到第 ") +
                          QString::number(line + 1) +
                          QStringLiteral(" 行 · 坏字节 ") +
                          QString::number(index + 1) + QStringLiteral("/") +
                          QString::number(lossyLines_.size()) +
                          QStringLiteral(" 处"));
  } else {
    statusLabel_->setText(
        QStringLiteral("坏字节位于第 ") + QString::number(line + 1) +
        QStringLiteral(" 行；当前预览未包含该行（可能超过 5 万字截断）"));
  }
}

void MainWindow::refreshSelectionAfterMove(int newSourceRow) {
  const QModelIndex proxyIdx =
      proxy_->mapFromSource(chapterModel_->index(newSourceRow, 0));
  if (proxyIdx.isValid()) {
    tableView_->setCurrentIndex(proxyIdx);
    tableView_->scrollTo(proxyIdx);
    previewSourceRow(newSourceRow);
  } else {
    // 被过滤：清空选择并显示空预览提示
    tableView_->clearSelection();
    setEmptyState();
  }
  updateControls();
}

void MainWindow::onMoveUp() {
  const QModelIndex cur = tableView_->currentIndex();
  if (!cur.isValid()) return;
  const int srcRow = proxy_->mapToSource(cur).row();
  if (srcRow <= 0) return;
  pushUndo();
  if (chapterModel_->moveRow(srcRow, srcRow - 1)) {
    setDirty(true);
    refreshSelectionAfterMove(srcRow - 1);
    updateStatusBar();
  } else {
    undoStack_.pop_back();
  }
}

void MainWindow::onMoveDown() {
  const QModelIndex cur = tableView_->currentIndex();
  if (!cur.isValid()) return;
  const int srcRow = proxy_->mapToSource(cur).row();
  const int n = static_cast<int>(chapterModel_->chapters().size());
  if (srcRow < 0 || srcRow >= n - 1) return;
  pushUndo();
  if (chapterModel_->moveRow(srcRow, srcRow + 1)) {
    setDirty(true);
    refreshSelectionAfterMove(srcRow + 1);
    updateStatusBar();
  } else {
    undoStack_.pop_back();
  }
}

void MainWindow::onResetOrder() {
  if (!isLoaded_) return;
  pushUndo();
  chapterModel_->resetChapters(initialChapters_);
  excludedRanges_.clear();
  textExclusions_.clear();
  setDirty(false);
  if (proxy_->rowCount() > 0) {
    tableView_->setCurrentIndex(proxy_->index(0, 0));
    previewSourceRow(0);
  } else {
    setEmptyState();
  }
  updateControls();
  updateStatusBar();
}

void MainWindow::onAcceptUnnumberedToggled(bool checked) {
  if (!isLoaded_) return;
  // 该开关会重新解析并清空编辑/撤销栈，必须先确认未导出修改。
  if (dirty_ && !confirmDiscardChanges()) {
    QSignalBlocker blocker(acceptUnnumbered_);
    acceptUnnumbered_->setChecked(!checked);
    updateAcceptUnnumberedButton();
    return;
  }

  statusLabel_->setText(QStringLiteral("正在重新识别章节…"));
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  ScopedWaitCursor wait;

  novel::ParseOptions opts;
  opts.accept_unnumbered_titles = checked;
  const novel::ParseResult parsed = novel::parse_chapters(loadedText_, opts);

  tocLines_ = parsed.toc_lines;
  initialChapters_ = parsed.chapters;
  warnings_ = encodingWarnings_;
  warnings_.insert(warnings_.end(), parsed.warnings.begin(),
                   parsed.warnings.end());
  chapterModel_->resetChapters(parsed.chapters);
  dirty_ = false;
  // 重新解析时清空撤销/重做与 excluded，重置基线
  undoStack_.clear();
  redoStack_.clear();
  excludedRanges_.clear();
  textExclusions_.clear();
  if (recoveryEnabled_ && !recovering_) clearRecoveryBackup();
  // tocLines_ 已变，影响预览跳行，缓存键不含 toc，必须失效。
  previewCache_.reset();

  if (proxy_->rowCount() > 0) {
    tableView_->setCurrentIndex(proxy_->index(0, 0));
    previewSourceRow(0);
  } else {
    setEmptyState();
  }

  updateChips();
  updateControls();
  updateStatusBar();
}

// ===== 撤销 / 重做 =====
std::size_t MainWindow::snapshotBytes(const Snapshot& snap) {
  std::size_t bytes = sizeof(Snapshot);
  bytes += snap.chapters.capacity() * sizeof(novel::Chapter);
  bytes += snap.excluded.capacity() * sizeof(novel::LineRange);
  bytes += snap.exclusions.capacity() * sizeof(novel::TextExclusion);
  for (const novel::Chapter& ch : snap.chapters) {
    bytes += ch.title.capacity() + ch.heading_line.capacity() +
             ch.number.capacity() + ch.name.capacity() +
             ch.separator.capacity();
    bytes += ch.skip_lines.capacity() * sizeof(std::size_t);
    bytes += ch.extra_body.capacity() * sizeof(novel::LineRange);
  }
  return bytes;
}

void MainWindow::trimUndoStack(std::vector<Snapshot>& stack,
                               std::size_t stepLimit,
                               std::size_t byteLimit) {
  std::size_t total = 0;
  for (Snapshot& snap : stack) {
    // 正常快照在建立时已写入 bytes；测试或旧路径构造的快照才补算一次。
    if (snap.bytes == 0) snap.bytes = snapshotBytes(snap);
    total += snap.bytes;
  }
  while (stack.size() > 1 &&
         (stack.size() > stepLimit || total > byteLimit)) {
    total -= stack.front().bytes;
    stack.erase(stack.begin());
  }
}

void MainWindow::pushUndo() {
  Snapshot snap;
  snap.chapters = chapterModel_->chapters();
  snap.excluded = excludedRanges_;
  snap.exclusions = textExclusions_;
  snap.selectedSourceRow = currentSourceRow();
  snap.bytes = snapshotBytes(snap);
  undoStack_.push_back(std::move(snap));
  trimUndoStack(undoStack_, kUndoLimit, kUndoBytesLimit);
  redoStack_.clear();
  updateControls();
}

void MainWindow::applySnapshot(const Snapshot& snap) {
  chapterModel_->resetChapters(snap.chapters);
  excludedRanges_ = snap.excluded;
  textExclusions_ = snap.exclusions;

  const int n = static_cast<int>(chapterModel_->chapters().size());
  int target = snap.selectedSourceRow;
  if (target < 0 || target >= n) target = n > 0 ? 0 : -1;
  if (target >= 0) {
    const QModelIndex idx =
        proxy_->mapFromSource(chapterModel_->index(target, 0));
    if (idx.isValid()) {
      tableView_->setCurrentIndex(idx);
      tableView_->scrollTo(idx);
      previewSourceRow(target);
    } else {
      tableView_->clearSelection();
      setEmptyState();
    }
  } else {
    tableView_->clearSelection();
    setEmptyState();
  }
  updateChips();
  updateControls();
  updateStatusBar();
}

void MainWindow::onUndo() { undo(); }
void MainWindow::onRedo() { redo(); }

void MainWindow::undo() {
  if (undoStack_.empty()) return;
  Snapshot cur;
  cur.chapters = chapterModel_->chapters();
  cur.excluded = excludedRanges_;
  cur.exclusions = textExclusions_;
  cur.selectedSourceRow = currentSourceRow();
  cur.bytes = snapshotBytes(cur);
  redoStack_.push_back(std::move(cur));
  trimUndoStack(redoStack_, kUndoLimit, kUndoBytesLimit);

  applySnapshot(undoStack_.back());
  undoStack_.pop_back();
  setDirty(!undoStack_.empty());
  updateControls();
}

void MainWindow::redo() {
  if (redoStack_.empty()) return;
  Snapshot cur;
  cur.chapters = chapterModel_->chapters();
  cur.excluded = excludedRanges_;
  cur.exclusions = textExclusions_;
  cur.selectedSourceRow = currentSourceRow();
  cur.bytes = snapshotBytes(cur);
  undoStack_.push_back(std::move(cur));
  trimUndoStack(undoStack_, kUndoLimit, kUndoBytesLimit);

  applySnapshot(redoStack_.back());
  redoStack_.pop_back();
  setDirty(true);
  updateControls();
}

// ===== 修改状态 =====
void MainWindow::setDirty(bool dirty) {
  dirty_ = dirty;
  if (dirty_) {
    scheduleRecoveryBackup();
  } else if (recoveryEnabled_ && !recovering_) {
    clearRecoveryBackup();
  }
  updateChips();
  updateStatusBar();
}

void MainWindow::markEdited() { setDirty(true); }

// ===== 编辑操作 =====
const novel::Chapter* MainWindow::currentChapter() const {
  const int src = currentSourceRow();
  if (src < 0) return nullptr;
  return &chapterModel_->chapters()[static_cast<std::size_t>(src)];
}

void MainWindow::onRenameChapter() {
  const int src = currentSourceRow();
  if (src < 0) return;
  std::vector<novel::Chapter>& chs = chapterModel_->chapters();
  novel::Chapter& ch = chs[static_cast<std::size_t>(src)];

  RenameFields fields;
  QDialog* dlg = ui::buildRenameDialog(this, themeDark_, ch, fields);
  const bool accepted = dlg->exec() == QDialog::Accepted;
  const QString number = fields.number->text().trimmed();
  const QString name = fields.name->text().trimmed();
  dlg->deleteLater();
  if (!accepted) return;

  pushUndo();
  if (novel::rename_chapter_parts(ch, number.toStdString(), name.toStdString())) {
    markEdited();
    chapterModel_->refresh();
    refreshSelectionAfterMove(src);
    updateStatusBar();
  } else {
    undoStack_.pop_back();
    updateControls();
  }
}

// 在预览文档上叠加「无效内容」删除线样式（文档中仍保留，可继续选中）。
// sourceToBlock[srcLine] = block 行号（-1 表示该源行不在当前预览中）。
void MainWindow::applyExclusionFormats(const QVector<int>& sourceToBlock) {
  if (textExclusions_.empty()) return;

  // 收集当前预览涉及的源行对应的无效区间，按行归并。
  std::map<std::size_t, std::vector<std::pair<std::size_t, std::size_t>>> byLine;
  for (const novel::TextExclusion& e : textExclusions_) {
    if (e.begin_byte >= e.end_byte) continue;
    const int blk = (e.line < static_cast<std::size_t>(sourceToBlock.size()))
 ? sourceToBlock[static_cast<int>(e.line)]
                        : -1;
    if (blk < 0) continue;  // 不在当前预览
    byLine[e.line].emplace_back(e.begin_byte, e.end_byte);
  }
  if (byLine.empty()) return;

  QTextDocument* doc = previewEdit_->document();
  QTextCharFormat inv;
  inv.setFontStrikeOut(true);
  inv.setForeground(C.invalid);

  for (const auto& [line, spans] : byLine) {
    const int blk = sourceToBlock[static_cast<int>(line)];
    const QTextBlock block = doc->findBlockByNumber(blk);
    if (!block.isValid()) continue;
    // 字节偏移 -> UTF-16 字符位置
    std::size_t cursor = 0;
    std::vector<std::pair<int, int>> charSpans;
    for (const auto& span : spans) {
      const std::size_t len = static_cast<std::size_t>(
          QString::fromUtf8(lines_[line].data(),
                           static_cast<int>(span.first)).size());
      const std::size_t lenEnd = static_cast<std::size_t>(
          QString::fromUtf8(lines_[line].data(),
                           static_cast<int>(span.second)).size());
      charSpans.emplace_back(static_cast<int>(cursor + len),
                             static_cast<int>(cursor + lenEnd));
    }
    QTextCursor c(doc);
    for (const auto& cs : charSpans) {
      c.setPosition(block.position() + cs.first);
      c.setPosition(block.position() + cs.second, QTextCursor::KeepAnchor);
      c.mergeCharFormat(inv);
    }
  }
}

// 预览选中文本后右键「设置为无效内容」：每行选中区间转成 TextExclusion。
void MainWindow::onSetInvalid() {
  if (!isLoaded_) return;
  QTextCursor cur = pendingInvalidSelection_.hasSelection()
 ? pendingInvalidSelection_
                        : previewEdit_->textCursor();
  pendingInvalidSelection_ = QTextCursor();
  if (!cur.hasSelection()) return;

  const int selStart = cur.selectionStart();
  const int selEnd = cur.selectionEnd();
  if (selStart == selEnd) return;

  std::vector<novel::TextExclusion> added;

  // 逐 block 解析选中区间：找出 start/end 所在的 block，分别 clamp 到块内。
  QTextDocument* doc = previewEdit_->document();
  QTextBlock bStart = doc->findBlock(selStart);
  QTextBlock bEnd = doc->findBlock(selEnd);
  if (!bStart.isValid() || !bEnd.isValid()) return;
  int startBlock = bStart.blockNumber();
  int endBlock = bEnd.blockNumber();

  // 仅保留当前章节的预览 block（排除「（无正文内容）」占位 / 截断提示）。
  // 用 blockToSource_ 直接定位，跳出不映射的 block。
  for (int blk = startBlock; blk <= endBlock; ++blk) {
    if (blk < 0 || blk >= blockToSource_.size()) continue;
    const int src = blockToSource_.at(blk);
    if (src < 0) continue;  // 非正文行（标题/占位/截断提示）
    const std::string_view raw = lines_[static_cast<std::size_t>(src)];
    const QTextBlock block = doc->findBlockByNumber(blk);
    const QString blockText = block.text();
    const int blockPos = block.position();
    int localStart = 0;
    int localEnd = blockText.size();
    if (blk == startBlock) {
      localStart = selStart - blockPos;
    }
    if (blk == endBlock) {
      localEnd = selEnd - blockPos;
    }
    if (localEnd > blockText.size()) localEnd = blockText.size();
    if (localStart < 0) localStart = 0;
    if (localStart >= localEnd) continue;

    // 计算字节偏移：块内前 localStart 个字符的 UTF-8 长度
    const QChar* dataChars = blockText.constData();
    const int beginByte = QString::fromRawData(dataChars, localStart)
                              .toUtf8()
                              .size();
    const int endByte = QString::fromRawData(dataChars, localEnd)
                            .toUtf8()
                            .size();
    if (beginByte >= endByte) continue;
    // 跳过整行纯空白选择
    const std::string_view sv = raw.substr(static_cast<std::size_t>(beginByte),
                                           static_cast<std::size_t>(endByte - beginByte));
    const std::string seg(sv.data(), sv.size());
    // 去除首尾空白后若为空，跳过
    std::size_t a = 0, b = seg.size();
    while (a < b && (seg[a] == ' ' || seg[a] == '\t' || seg[a] == '\r' ||
                     seg[a] == '\n' || seg[a] == '\f')) ++a;
    while (b > a && (seg[b - 1] == ' ' || seg[b - 1] == '\t' ||
                     seg[b - 1] == '\r' || seg[b - 1] == '\n' ||
                     seg[b - 1] == '\f')) --b;
    if (a >= b) continue;

    added.push_back({static_cast<std::size_t>(src),
                     static_cast<std::size_t>(beginByte),
                     static_cast<std::size_t>(endByte)});
  }

  if (added.empty()) {
    statusLabel_->setText(
        QStringLiteral("请先在正文中选择要标记为无效内容的文本"));
    return;
  }

  // 合并 / 去重：按 (line, begin) 排序；同一行重叠区间合并。
  std::sort(added.begin(), added.end(),
            [](const novel::TextExclusion& a, const novel::TextExclusion& b) {
              if (a.line != b.line) return a.line < b.line;
              return a.begin_byte < b.begin_byte;
            });
  std::vector<novel::TextExclusion> merged;
  merged.reserve(added.size());
  for (const novel::TextExclusion& e : added) {
    if (!merged.empty() && merged.back().line == e.line &&
        e.begin_byte <= merged.back().end_byte) {
      merged.back().end_byte = std::max(merged.back().end_byte, e.end_byte);
    } else {
      merged.push_back(e);
    }
  }

  pushUndo();
  textExclusions_.insert(textExclusions_.end(), merged.begin(), merged.end());
  setDirty(true);
  // 重置预览缓存并重建当前章：应用新的删除线样式。
  previewCache_.reset();
  lastPreviewSelection_ = QTextCursor();
  const int curRow = currentSourceRow();
  if (curRow >= 0) previewSourceRow(curRow);
  updateStatusBar();
  statusLabel_->setText(
      QStringLiteral("已将选中文本标记为无效内容（导出时丢弃）"));
}

void MainWindow::onToggleExcluded(bool excluded) {
  const int src = currentSourceRow();
  if (src < 0) return;
  std::vector<novel::Chapter>& chs = chapterModel_->chapters();
  novel::Chapter& ch = chs[static_cast<std::size_t>(src)];
  if (!ch.numbered) return;
  if (ch.excluded_from_numbering == excluded) return;

  pushUndo();
  novel::set_excluded_from_numbering(ch, excluded);
  markEdited();
  chapterModel_->refresh();
  refreshSelectionAfterMove(src);
  updateStatusBar();
}

void MainWindow::onJoinNumbering() {
  const int src = currentSourceRow();
  if (src < 0) return;
  std::vector<novel::Chapter>& chs = chapterModel_->chapters();
  if (chs[static_cast<std::size_t>(src)].numbered) return;

  pushUndo();
  if (novel::promote_to_numbering(chs, src)) {
    markEdited();
    chapterModel_->refresh();
    refreshSelectionAfterMove(src);
    updateStatusBar();
    statusLabel_->setText(
        QStringLiteral("已设为参与排序：") +
        QString::fromStdString(chs[static_cast<std::size_t>(src)].title));
  } else {
    undoStack_.pop_back();
    updateControls();
    statusLabel_->setText(QStringLiteral("无法将该章设为参与排序"));
  }
}

void MainWindow::onMoveBodyToPrevious() {
  const int src = currentSourceRow();
  if (src <= 0) return;
  std::vector<novel::Chapter>& chs = chapterModel_->chapters();
  pushUndo();
  if (novel::move_body_to_previous(chs, src)) {
    markEdited();
    chapterModel_->refresh();
    // 正文已划走，当前章只剩标题：预览需重建。
    previewCache_.reset();
    refreshSelectionAfterMove(src);
    updateStatusBar();
  } else {
    undoStack_.pop_back();
    updateControls();
    statusLabel_->setText(QStringLiteral("当前章没有可划走的正文"));
  }
}

void MainWindow::onMoveBodyToNext() {
  const int src = currentSourceRow();
  if (src < 0) return;
  const int n = static_cast<int>(chapterModel_->chapters().size());
  if (src >= n - 1) return;
  std::vector<novel::Chapter>& chs = chapterModel_->chapters();
  pushUndo();
  if (novel::move_body_to_next(chs, src)) {
    markEdited();
    chapterModel_->refresh();
    previewCache_.reset();
    refreshSelectionAfterMove(src);
    updateStatusBar();
  } else {
    undoStack_.pop_back();
    updateControls();
    statusLabel_->setText(QStringLiteral("当前章没有可划走的正文"));
  }
}

void MainWindow::onDeleteChapter() {
  const int src = currentSourceRow();
  if (src < 0) return;
  const std::vector<novel::Chapter>& chs = chapterModel_->chapters();
  const novel::Chapter& ch = chs[static_cast<std::size_t>(src)];
  const int lineCount =
      static_cast<int>(ch.end_line) - static_cast<int>(ch.start_line);
  QMessageBox box(this);
  box.setIcon(QMessageBox::Question);
  box.setWindowTitle(QStringLiteral("删除整章"));
  box.setText(QStringLiteral("删除「") + QString::fromStdString(ch.title) +
              QStringLiteral("」？\n\n共 ") + QString::number(lineCount) +
              QStringLiteral(" 行，标题与正文都不导出。"));
  QPushButton* deleteButton =
      box.addButton(QStringLiteral("确定删除"), QMessageBox::DestructiveRole);
  QPushButton* stayButton =
      box.addButton(QStringLiteral("返回继续"), QMessageBox::RejectRole);
  box.setDefaultButton(stayButton);
  box.setEscapeButton(stayButton);
  box.exec();
  if (box.clickedButton() != deleteButton) return;

  pushUndo();
  std::vector<novel::Chapter>& mchs = chapterModel_->chapters();
  if (novel::delete_chapter(mchs, src, excludedRanges_)) {
    markEdited();
    chapterModel_->refresh();
    const int n = static_cast<int>(mchs.size());
    int sel = src;
    if (sel >= n) sel = n - 1;
    refreshSelectionAfterMove(sel >= 0 ? sel : -1);
    updateStatusBar();
  } else {
    undoStack_.pop_back();
    updateControls();
    show_warning_message(this, QStringLiteral("无法删除"),
                         QStringLiteral("当前章节无法删除，文件内容未改动。"));
  }
}

void MainWindow::renumberDialog() {
  if (!isLoaded_) return;
  RenumberFields fields;
  QDialog* dlg = ui::buildRenumberDialog(this, themeDark_, fields);
  const bool accepted = dlg->exec() == QDialog::Accepted;
  const bool chinese = fields.style->currentData().toBool();
  const int start = fields.start->value();
  const int pad = chinese ? 0 : fields.pad->currentData().toInt();
  dlg->deleteLater();
  if (!accepted) return;

  const int keepSel = currentSourceRow();
  pushUndo();
  std::vector<novel::Chapter>& chs = chapterModel_->chapters();
  if (novel::renumber_chapters(chs, start, chinese, pad)) {
    markEdited();
    chapterModel_->refresh();
    // 保持当前选中
    const int n = static_cast<int>(chs.size());
    int sel = keepSel;
    if (sel < 0 || sel >= n) sel = n > 0 ? 0 : -1;
    if (sel >= 0) {
      const QModelIndex idx =
          proxy_->mapFromSource(chapterModel_->index(sel, 0));
      if (idx.isValid()) {
        tableView_->setCurrentIndex(idx);
        previewSourceRow(sel);
      } else {
        previewSourceRow(sel);
      }
    }
    updateControls();
    updateStatusBar();
  } else {
    undoStack_.pop_back();
  }
}

void MainWindow::onRenumber() { renumberDialog(); }

// ===== 拖拽排序 =====
void MainWindow::onRowDropped(int fromRow, int toRow) {
  const int n = static_cast<int>(chapterModel_->chapters().size());
  if (fromRow < 0 || fromRow >= proxy_->rowCount()) return;
  const int sourceFrom = proxy_->mapToSource(proxy_->index(fromRow, 0)).row();
  if (sourceFrom < 0 || sourceFrom >= n) return;
  int target = n;
  if (toRow >= 0 && toRow < proxy_->rowCount()) {
    target = proxy_->mapToSource(proxy_->index(toRow, 0)).row();
  }
  if (target < 0) target = 0;
  if (target > n) target = n;
  if (target == sourceFrom || target == sourceFrom + 1) return;
  pushUndo();
  if (chapterModel_->moveRow(sourceFrom,
                            target > sourceFrom ? target - 1 : target)) {
    setDirty(true);
    int landed = target > sourceFrom ? target - 1 : target;
    if (landed < 0) landed = 0;
    if (landed > n - 1) landed = n - 1;
    const QModelIndex idx =
        proxy_->mapFromSource(chapterModel_->index(landed, 0));
    if (idx.isValid()) {
      tableView_->setCurrentIndex(idx);
      tableView_->scrollTo(idx);
      previewSourceRow(landed);
    } else {
      tableView_->clearSelection();
      setEmptyState();
    }
    updateStatusBar();
  } else {
    undoStack_.pop_back();
  }
}

// ===== 右键菜单 =====
QMenu* MainWindow::buildChapterMenu(bool onItem, int row) {
  QMenu* menu = new QMenu(this);
  if (!onItem) return menu;
  const std::vector<novel::Chapter>& chs = chapterModel_->chapters();
  if (row < 0 || row >= static_cast<int>(chs.size())) return menu;
  const novel::Chapter& ch = chs[static_cast<std::size_t>(row)];
  const int n = static_cast<int>(chs.size());

  QAction* aRename = menu->addAction(QStringLiteral("重命名…"));
  connect(aRename, &QAction::triggered, this, &MainWindow::onRenameChapter);

  menu->addSeparator();

  if (ch.numbered) {
    QAction* aExclude = menu->addAction(QStringLiteral("不参与排序"));
    aExclude->setCheckable(true);
    aExclude->setChecked(ch.excluded_from_numbering);
    connect(aExclude, &QAction::toggled, this,
            &MainWindow::onToggleExcluded);
  } else {
    QAction* aJoin = menu->addAction(QStringLiteral("参与排序"));
    aJoin->setToolTip(
        QStringLiteral("自动占用下一个序号，后续章节序号顺延"));
    connect(aJoin, &QAction::triggered, this, &MainWindow::onJoinNumbering);
  }

  QAction* aPrev = menu->addAction(QStringLiteral("划为上一章内容"));
  aPrev->setEnabled(row > 0);
  connect(aPrev, &QAction::triggered, this,
          &MainWindow::onMoveBodyToPrevious);

  QAction* aNext = menu->addAction(QStringLiteral("划为下一章内容"));
  aNext->setEnabled(row < n - 1);
  connect(aNext, &QAction::triggered, this, &MainWindow::onMoveBodyToNext);

  menu->addSeparator();

  QAction* aDelete = menu->addAction(QStringLiteral("整章内容都删除…"));
  connect(aDelete, &QAction::triggered, this, &MainWindow::onDeleteChapter);
  return menu;
}

void MainWindow::onListContextMenu(const QPoint& pos) {
  if (!isLoaded_) return;
  const QModelIndex idx = tableView_->indexAt(pos);
  int row = -1;
  bool onItem = false;
  if (idx.isValid()) {
    row = proxy_->mapToSource(idx).row();
    onItem = true;
    tableView_->setCurrentIndex(idx);
  }
  QMenu* menu = buildChapterMenu(onItem, row);
  menu->exec(tableView_->viewport()->mapToGlobal(pos));
}

void MainWindow::onPreviewContextMenu(const QPoint& pos) {
  if (!isLoaded_) return;
  QMenu* menu = new QMenu(this);

  QAction* aSplit = new QAction(QStringLiteral("在此拆分章节…"), menu);
  connect(aSplit, &QAction::triggered, this, &MainWindow::onSplitHere);
  menu->addAction(aSplit);

  // Qt 在某些右键/键盘菜单路径下会先清空 textCursor 的选择；
  // 使用最近一次非空选择兜底，保证「设置为无效内容」可用。
  QTextCursor selCur = previewEdit_->textCursor();
  if (!selCur.hasSelection() && lastPreviewSelection_.hasSelection()) {
    selCur = lastPreviewSelection_;
  }
  pendingInvalidSelection_ = selCur;
  const bool hasSel = selCur.hasSelection();
  QAction* aInvalid = new QAction(QStringLiteral("设置为无效内容"), menu);
  aInvalid->setEnabled(hasSel);
  connect(aInvalid, &QAction::triggered, this, &MainWindow::onSetInvalid);
  menu->addAction(aInvalid);

  menu->exec(previewEdit_->mapToGlobal(pos));
}

void MainWindow::onSplitHere() {
  const int src = currentSourceRow();
  if (src < 0) return;
  // 取光标所在行
  QTextCursor cur = previewEdit_->textCursor();
  const int srcLine = previewCursorToSource(cur);
  std::vector<novel::Chapter>& chs = chapterModel_->chapters();
  const novel::Chapter current = chs[static_cast<std::size_t>(src)];

  if (srcLine < 0) {
    show_warning_message(this, QStringLiteral("无法拆分"),
                         QStringLiteral("光标不在有效正文行上，无法拆分。"));
    return;
  }
  const std::size_t sLine = static_cast<std::size_t>(srcLine);
  if (sLine >= lines_.size()) return;
  for (const novel::LineRange& range : current.extra_body) {
    if (sLine >= range.begin && sLine < range.end) {
      show_warning_message(
          this, QStringLiteral("无法拆分"),
          QStringLiteral("这一行是划入本章的前置内容，不能作为新章节起点。"));
      return;
    }
  }
  if (qs(lines_[sLine]).trimmed().isEmpty()) {
    show_warning_message(this, QStringLiteral("无法拆分"),
                         QStringLiteral("光标的这一行是空行，不能作为新章节起点。"));
    return;
  }
  if (!(current.start_line < sLine && sLine < current.end_line)) {
    show_warning_message(
        this, QStringLiteral("无法拆分"),
        QStringLiteral("光标所在的行不在当前章节正文范围内（标题行或越界）。"));
    return;
  }

  // 无编号当前章（如前言）拆分：建议使用下一个参与排序章的序号；
  // 新章勾选参与后，该序号归新章，后续章节由 core 自动 +1。
  QString suggestedNumber;
  for (int k = src + 1; k < static_cast<int>(chs.size()); ++k) {
    const novel::Chapter& next = chs[static_cast<std::size_t>(k)];
    if (next.numbered && !next.excluded_from_numbering) {
      suggestedNumber = qs(next.number);
      break;
    }
  }
  SplitFields fields;
  QDialog* dlg = ui::buildSplitDialog(this, themeDark_, current,
                                     suggestedNumber, fields);
  const bool accepted = dlg->exec() == QDialog::Accepted;
  const bool participate =
      fields.autoNumber ||
      (fields.participate != nullptr && fields.participate->isChecked());
  const QString number =
      fields.autoNumber
 ? QString()
          : (participate ? fields.number->text().trimmed() : QString());
  const QString name = fields.name->text().trimmed();
  dlg->deleteLater();
  if (!accepted) return;

  pushUndo();
  if (novel::split_chapter_numbered(chs, src, sLine, number.toStdString(),
                                    name.toStdString(), current.level)) {
    markEdited();
    chapterModel_->refresh();
    previewCache_.reset();
    // 新章为原章后面的那一章
    refreshSelectionAfterMove(src + 1);
    updateStatusBar();
  } else {
    undoStack_.pop_back();
    updateControls();
    show_warning_message(this, QStringLiteral("无法拆分"),
                         QStringLiteral("拆分失败，可能该行不允许作为章节起点。"));
  }
}

// ===== 导出 =====
bool MainWindow::exportContent(bool json) {
  if (!isLoaded_) return false;

  const QString sourcePath = QString::fromStdString(novel::path_to_utf8(currentFile_));
  const QFileInfo fi(sourcePath);
  const QString base = fi.completeBaseName().isEmpty()
 ? QStringLiteral("book")
                           : fi.completeBaseName();
  const QString suffix = json ? QStringLiteral(".json") : QStringLiteral(".txt");
  const QString filter = json ? QStringLiteral("JSON 文件 (*.json)")
                              : QStringLiteral("文本文件 (*.txt)");

  const bool toExec = !json && exportToExecDir_ && exportToExecDir_->isChecked();
  static QString s_mruDir;
  QString dir = fi.absolutePath();
  if (!s_mruDir.isEmpty()) {
    dir = QFileInfo(s_mruDir).absolutePath();
  } else if (dir.isEmpty() || dir == QStringLiteral(".")) {
    dir = QDir::homePath();
  }
  const QString suggested = dir + QDir::separator() + base +
                           QStringLiteral("_reordered") + suffix;

  QString path;
  if (toExec) {
    path = QCoreApplication::applicationDirPath() + QDir::separator() + base +
           QStringLiteral("_reordered") + suffix;
  } else {
    path = QFileDialog::getSaveFileName(
        this, json ? QStringLiteral("导出目录 JSON") : QStringLiteral("导出 TXT"),
        suggested, filter);
    if (!path.isEmpty()) s_mruDir = path;
  }
  if (path.isEmpty()) return false;

  // 导出到程序位置且目标已存在：覆盖 / 另存为 / 取消。
  if (toExec && QFile::exists(path)) {
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QStringLiteral("文件已存在"));
    box.setText(QStringLiteral("程序位置已存在：\n") + path +
                QStringLiteral("\n\n请选择处理方式。"));
    QPushButton* overwrite = box.addButton(QStringLiteral("覆盖"),
                                           QMessageBox::DestructiveRole);
    QPushButton* saveAs = box.addButton(QStringLiteral("另存为"),
                                        QMessageBox::ActionRole);
    QPushButton* cancel = box.addButton(QStringLiteral("取消"),
                                        QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.setEscapeButton(cancel);
    box.exec();
    const QAbstractButton* clicked = box.clickedButton();
    if (clicked == cancel || clicked == nullptr) return false;
    if (clicked == saveAs) {
      const QString alt = QFileDialog::getSaveFileName(
          this, QStringLiteral("另存为"), path, filter);
      if (alt.isEmpty()) return false;
      path = alt;
    }
    (void)overwrite;
  }

  statusLabel_->setText(QStringLiteral("正在导出：") + path);
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  ScopedWaitCursor wait;

  const std::string content =
      json ? novel::directory_json(
                 novel::DirectoryJsonInput{novel::path_to_utf8(currentFile_),
                                           loadedEncoding_,
                                           &chapterModel_->chapters(),
                                           &tocLines_, &warnings_})
           : novel::render_txt(loadedText_, chapterModel_->chapters(),
                              tocLines_, excludedRanges_, textExclusions_);

  QSaveFile out(path);
  if (!out.open(QIODevice::WriteOnly)) {
    wait.restore();
    show_warning_message(this, QStringLiteral("导出失败"),
                         QStringLiteral("无法写入文件：\n") + path + "\n" +
                             out.errorString());
    return false;
  }
  const qint64 written =
      out.write(content.data(), static_cast<qint64>(content.size()));
  if (written != static_cast<qint64>(content.size())) {
    out.cancelWriting();
    wait.restore();
    show_warning_message(this, QStringLiteral("导出失败"),
                         QStringLiteral("写入不完整：\n") + path);
    return false;
  }
  if (!out.commit()) {
    wait.restore();
    show_warning_message(this, QStringLiteral("导出失败"),
                         QStringLiteral("无法完成写入：\n") + path + "\n" +
                             out.errorString());
    return false;
  }

  if (!json) {
    QSettings settings = makeSettings();
    settings.setValue(QStringLiteral("exportToExecDir"),
                     exportToExecDir_->isChecked());
  }

  lastExportedPath_ = path;
  if (btns_->openExportLoc) btns_->openExportLoc->setEnabled(true);
  setDirty(false);
  statusLabel_->setText(QStringLiteral("已导出：") + path);
  return true;
}

void MainWindow::onExportTxt() {
  // 成功后只更新状态栏并启用「打开导出位置」，不弹模态框打断操作。
  exportContent(false);
}

void MainWindow::onExportJson() { exportContent(true); }

void MainWindow::onOpenExportLocation() {
  if (lastExportedPath_.isEmpty()) return;
  if (!QFile::exists(lastExportedPath_)) {
    show_warning_message(
        this, QStringLiteral("导出文件不存在"),
        QStringLiteral("文件已被移动或删除：\n") + lastExportedPath_ +
            QStringLiteral("\n\n请重新导出后再打开位置。"));
    lastExportedPath_.clear();
    if (btns_->openExportLoc) btns_->openExportLoc->setEnabled(false);
    return;
  }
#ifdef _WIN32
  const QString arg = QStringLiteral("/select,") +
                     QDir::toNativeSeparators(lastExportedPath_);
  ShellExecuteW(nullptr, nullptr, L"explorer.exe", arg.toStdWString().c_str(),
                nullptr, SW_SHOW);
#else
  QDesktopServices::openUrl(
      QUrl::fromLocalFile(QFileInfo(lastExportedPath_).absolutePath()));
#endif
}

QString MainWindow::recoveryFilePath() const {
  QString dir =
      QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
  if (dir.isEmpty()) dir = QDir::tempPath();
  QDir().mkpath(dir);
  return dir + QDir::separator() + QStringLiteral("recovery.bin");
}

void MainWindow::clearRecoveryBackup() {
  if (recoveryTimer_) recoveryTimer_->stop();
  const QString path = recoveryFilePath();
  if (QFile::exists(path)) QFile::remove(path);
}

void MainWindow::scheduleRecoveryBackup() {
  if (!recoveryEnabled_ || recovering_ || !isLoaded_ || !dirty_ ||
      !recoveryTimer_) {
    return;
  }
  if (!recoveryTimer_->isActive()) {
    recoveryTimer_->start(metrics::kRecoveryDebounceMs);
  }
}

bool MainWindow::writeRecoveryBackup() {
  if (!recoveryEnabled_ || recovering_ || !isLoaded_ || !dirty_) return false;
  const QString sourcePath = QString::fromStdString(novel::path_to_utf8(currentFile_));
  const QFileInfo sourceInfo(sourcePath);
  if (!sourceInfo.exists()) return false;

  RecoveryData snapshot;
  snapshot.sourcePath = sourcePath;
  snapshot.sourceSize = sourceInfo.size();
  snapshot.sourceMTime = sourceInfo.lastModified().toMSecsSinceEpoch();
  snapshot.savedAtMs = QDateTime::currentMSecsSinceEpoch();
  snapshot.encoding = QString::fromStdString(loadedEncoding_);
  snapshot.acceptUnnumbered =
      acceptUnnumbered_ && acceptUnnumbered_->isChecked();
  snapshot.selectedSourceRow = currentSourceRow();
  snapshot.chapters = chapterModel_->chapters();
  snapshot.excluded = excludedRanges_;
  snapshot.exclusions = textExclusions_;
  return writeRecovery(recoveryFilePath(), snapshot);
}

bool MainWindow::readRecoveryBackup(RecoveryData& backup) const {
  return readRecovery(recoveryFilePath(), backup);
}

bool MainWindow::tryRecoverBackup() {
  if (!recoveryEnabled_ || recovering_) return false;
  if (!QFile::exists(recoveryFilePath())) return false;

  RecoveryData backup;
  if (!readRecoveryBackup(backup)) {
    QFile::remove(recoveryFilePath());
    show_warning_message(
        this, QStringLiteral("自动备份损坏"),
        QStringLiteral("自动备份文件无法读取，已删除。"));
    return false;
  }

  const QFileInfo sourceInfo(backup.sourcePath);
  const bool sourceOk =
      sourceInfo.exists() && sourceInfo.size() == backup.sourceSize &&
      sourceInfo.lastModified().toMSecsSinceEpoch() == backup.sourceMTime;
  if (!sourceOk) {
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("自动备份无法恢复"));
    box.setText(
        QStringLiteral("自动备份对应的原文件已移动、删除或被修改，"
                       "无法安全恢复：\n") +
        backup.sourcePath);
    QPushButton* remove =
        box.addButton(QStringLiteral("删除备份"), QMessageBox::DestructiveRole);
    QPushButton* keep =
        box.addButton(QStringLiteral("保留备份"), QMessageBox::RejectRole);
    box.setDefaultButton(keep);
    box.setEscapeButton(keep);
    box.exec();
    if (box.clickedButton() == remove) clearRecoveryBackup();
    return false;
  }

  const QDateTime savedAt = QDateTime::fromMSecsSinceEpoch(backup.savedAtMs);
  QMessageBox box(this);
  box.setIcon(QMessageBox::Question);
  box.setWindowTitle(QStringLiteral("发现自动备份"));
  box.setText(QStringLiteral("上次会话未正常结束，发现未保存的修改：\n") +
              backup.sourcePath + QStringLiteral("\n时间：") +
              savedAt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")) +
              QStringLiteral("\n\n是否恢复？"));
  QPushButton* restore =
      box.addButton(QStringLiteral("恢复备份"), QMessageBox::AcceptRole);
  QPushButton* discard =
      box.addButton(QStringLiteral("丢弃备份"), QMessageBox::DestructiveRole);
  box.setDefaultButton(restore);
  box.setEscapeButton(discard);
  box.exec();
  if (box.clickedButton() != restore) {
    clearRecoveryBackup();
    return false;
  }

  novel::TextLoadResult loaded =
      novel::load_text_file(novel::path_from_utf8(backup.sourcePath.toStdString()));
  if (!loaded.ok) {
    show_warning_message(this, QStringLiteral("恢复失败"),
                         QString::fromStdString(loaded.error));
    return false;
  }

  recovering_ = true;
  loadedText_ = std::move(loaded.text);
  loadedEncoding_ = loaded.encoding;
  encodingWarnings_ = loaded.warnings;
  warnings_ = encodingWarnings_;
  warningBannerDismissed_ = false;
  currentFile_ = novel::path_from_utf8(backup.sourcePath.toStdString());
  lines_ = novel::split_lines_view(loadedText_);
  rebuildLossyLines();

  if (acceptUnnumbered_) {
    QSignalBlocker blocker(acceptUnnumbered_);
    acceptUnnumbered_->setChecked(backup.acceptUnnumbered);
    updateAcceptUnnumberedButton();
  }
  novel::ParseOptions opts;
  opts.accept_unnumbered_titles =
      acceptUnnumbered_ && acceptUnnumbered_->isChecked();
  const novel::ParseResult parsed = novel::parse_chapters(loadedText_, opts);
  tocLines_ = parsed.toc_lines;
  initialChapters_ = parsed.chapters;
  warnings_.insert(warnings_.end(), parsed.warnings.begin(),
                   parsed.warnings.end());

  chapterModel_->resetChapters(backup.chapters);
  excludedRanges_ = backup.excluded;
  textExclusions_ = backup.exclusions;
  undoStack_.clear();
  redoStack_.clear();
  searchEdit_->clear();
  previewCache_.reset();
  isLoaded_ = true;
  dirty_ = true;

  if (fileLabel_) {
    fileLabel_->setText(QFileInfo(backup.sourcePath).fileName());
    fileLabel_->setToolTip(QDir::toNativeSeparators(backup.sourcePath));
  }

  int target = backup.selectedSourceRow;
  const int count = static_cast<int>(chapterModel_->chapters().size());
  if (target < 0 || target >= count) target = count > 0 ? 0 : -1;
  if (target >= 0) {
    const QModelIndex idx =
        proxy_->mapFromSource(chapterModel_->index(target, 0));
    if (idx.isValid()) {
      tableView_->setCurrentIndex(idx);
      tableView_->scrollTo(idx);
      previewSourceRow(target);
    } else {
      setEmptyState();
    }
  } else {
    setEmptyState();
  }
  updateChips();
  updateControls();
  updateStatusBar();
  statusLabel_->setText(QStringLiteral("已从自动备份恢复：") +
                        QFileInfo(backup.sourcePath).fileName());
  recovering_ = false;
  scheduleRecoveryBackup();
  return true;
}

void MainWindow::showLoadError(const std::string& error) {
  show_warning_message(this, QStringLiteral("无法打开文件"),
                       QString::fromStdString(error));
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
  if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
  const QList<QUrl> urls = event->mimeData()->urls();
  if (urls.isEmpty()) return;
  const QUrl url = urls.first();
  if (!url.isLocalFile()) return;
  if (!confirmDiscardChanges()) {
    event->ignore();
    return;
  }

  const QString path = url.toLocalFile();
  statusLabel_->setText(QStringLiteral("正在打开：") + path);
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  ScopedWaitCursor wait;
  loadFile(novel::path_from_utf8(path.toStdString()));
  event->acceptProposedAction();
}

void MainWindow::closeEvent(QCloseEvent* event) {
  if (dirty_) {
    // 先落盘，防止用户在确认框期间或之后异常退出导致最后修改丢失。
    if (recoveryEnabled_ && !recovering_) writeRecoveryBackup();
    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(QStringLiteral("尚未导出"));
    box.setText(QStringLiteral("内容已修改但未导出，仍要关闭吗？"));
    QPushButton* discard =
        box.addButton(QStringLiteral("放弃修改"), QMessageBox::DestructiveRole);
    QPushButton* stay =
        box.addButton(QStringLiteral("留下继续"), QMessageBox::RejectRole);
    box.setDefaultButton(stay);
    box.setEscapeButton(stay);
    box.exec();
    if (box.clickedButton() != discard) {
      event->ignore();
      return;
    }
    if (recoveryEnabled_ && !recovering_) clearRecoveryBackup();
  } else if (recoveryEnabled_ && !recovering_) {
    clearRecoveryBackup();
  }
  event->accept();
}


// ===== 截图模式（开发用） =====

}  // namespace ui
