#include "ui/DialoguePage.hpp"

#include <QAbstractItemView>
#include <QAbstractTableModel>
#include <QApplication>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QTableView>
#include <QTextEdit>
#include <QVBoxLayout>

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/ChapterParser.hpp"
#include "core/Script.hpp"
#include "core/TextFile.hpp"
#include "ui/Theme.hpp"
#include "ui/UiMetrics.hpp"

namespace ui {

namespace {

QString segmentTypeName(novel::ScriptSegmentType type) {
  switch (type) {
    case novel::ScriptSegmentType::heading:
      return QStringLiteral("标题");
    case novel::ScriptSegmentType::narration:
      return QStringLiteral("旁白");
    case novel::ScriptSegmentType::dialogue:
      return QStringLiteral("对白");
    case novel::ScriptSegmentType::thought:
      return QStringLiteral("心声");
  }
  return QStringLiteral("旁白");
}

QString toQString(std::string_view text) {
  return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

QString toQString(const std::string& text) {
  return toQString(std::string_view(text));
}

std::string toStdString(const QString& text) {
  const QByteArray bytes = text.toUtf8();
  return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

struct WaitCursor {
  WaitCursor() { QApplication::setOverrideCursor(Qt::WaitCursor); }
  ~WaitCursor() { QApplication::restoreOverrideCursor(); }
};

}  // namespace

class SegmentTableModel : public QAbstractTableModel {
  Q_OBJECT

 public:
  enum Column {
    ColIndex,
    ColType,
    ColSpeaker,
    ColText,
    ColConfidence,
    ColCount
  };

  explicit SegmentTableModel(QObject* parent = nullptr)
      : QAbstractTableModel(parent) {}

  void setSegments(const std::vector<novel::ScriptSegment>* segments) {
    beginResetModel();
    segments_ = segments;
    endResetModel();
  }

  int rowCount(const QModelIndex& parent = QModelIndex()) const override {
    if (parent.isValid() || segments_ == nullptr) return 0;
    return static_cast<int>(segments_->size());
  }

  int columnCount(const QModelIndex& parent = QModelIndex()) const override {
    return parent.isValid() ? 0 : ColCount;
  }

  QVariant headerData(int section, Qt::Orientation orientation,
                      int role = Qt::DisplayRole) const override {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) {
      return QAbstractTableModel::headerData(section, orientation, role);
    }
    switch (section) {
      case ColIndex:
        return QStringLiteral("序号");
      case ColType:
        return QStringLiteral("类型");
      case ColSpeaker:
        return QStringLiteral("说话人提示");
      case ColText:
        return QStringLiteral("文本");
      case ColConfidence:
        return QStringLiteral("置信度");
    }
    return {};
  }

  QVariant data(const QModelIndex& index,
                int role = Qt::DisplayRole) const override {
    if (segments_ == nullptr || !index.isValid()) return {};
    const novel::ScriptSegment& segment =
        (*segments_)[static_cast<std::size_t>(index.row())];

    if (role == Qt::TextAlignmentRole) {
      if (index.column() == ColIndex || index.column() == ColType ||
          index.column() == ColConfidence) {
        return static_cast<int>(Qt::AlignCenter);
      }
      return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
    }
    if (role == Qt::ToolTipRole && index.column() == ColText) {
      return toQString(segment.text);
    }
    if (role != Qt::DisplayRole) return {};

    switch (index.column()) {
      case ColIndex:
        return index.row() + 1;
      case ColType:
        return segmentTypeName(segment.type);
      case ColSpeaker:
        return segment.speaker_hint.empty() ? QStringLiteral("—")
                                           : toQString(segment.speaker_hint);
      case ColText:
        return toQString(segment.text);
      case ColConfidence:
        return QString::number(static_cast<double>(segment.confidence), 'f', 2);
    }
    return {};
  }

  Qt::ItemFlags flags(const QModelIndex& index) const override {
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
  }

 private:
  const std::vector<novel::ScriptSegment>* segments_ = nullptr;
};

DialoguePage::DialoguePage(QWidget* parent) : QWidget(parent) { buildUi(); }

void DialoguePage::buildUi() {
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  auto* topBar = new QFrame(this);
  topBar->setObjectName(QStringLiteral("topBar"));
  topBar->setFixedHeight(metrics::kTopBarHeight);
  auto* topLayout = new QHBoxLayout(topBar);
  topLayout->setContentsMargins(14, 0, 14, 0);
  topLayout->setSpacing(10);

  auto* openButton = new QPushButton(QStringLiteral("打开 TXT"), topBar);
  openButton->setObjectName(QStringLiteral("icon"));
  openButton->setCursor(Qt::PointingHandCursor);
  connect(openButton, &QPushButton::clicked, this, &DialoguePage::onOpenClicked);

  fileLabel_ = new QLabel(QStringLiteral("未打开文件"), topBar);
  fileLabel_->setObjectName(QStringLiteral("fileLabel"));

  topLayout->addWidget(openButton);
  topLayout->addWidget(fileLabel_, 1);

  reSplitBtn_ = new QPushButton(QStringLiteral("重新拆分"), topBar);
  reSplitBtn_->setCursor(Qt::PointingHandCursor);
  reSplitBtn_->setEnabled(false);
  connect(reSplitBtn_, &QPushButton::clicked, this,
          &DialoguePage::onReSplitClicked);

  exportBtn_ = new QPushButton(QStringLiteral("导出脚本"), topBar);
  exportBtn_->setProperty("class", "primary");
  exportBtn_->setCursor(Qt::PointingHandCursor);
  exportBtn_->setEnabled(false);
  connect(exportBtn_, &QPushButton::clicked, this, &DialoguePage::onExportClicked);

  topLayout->addWidget(reSplitBtn_);
  topLayout->addWidget(exportBtn_);
  root->addWidget(topBar);

  auto* statBar = new QFrame(this);
  statBar->setObjectName(QStringLiteral("topBar"));
  auto* statLayout = new QHBoxLayout(statBar);
  statLayout->setContentsMargins(14, 6, 14, 6);
  statLayout->setSpacing(18);

  const auto makeStat = [&](const QString& name) {
    auto* value = new QLabel(QStringLiteral("0"), statBar);
    value->setObjectName(QStringLiteral("appTitle"));
    auto* label = new QLabel(name, statBar);
    label->setObjectName(QStringLiteral("subTitle"));
    auto* box = new QHBoxLayout;
    box->setSpacing(4);
    box->addWidget(value);
    box->addWidget(label);
    statLayout->addLayout(box);
    return value;
  };
  statChapters_ = makeStat(QStringLiteral("章节总数"));
  statBuilt_ = makeStat(QStringLiteral("已拆分"));
  statSegments_ = makeStat(QStringLiteral("当前章片段"));
  statDialogue_ = makeStat(QStringLiteral("当前章对白"));
  statThought_ = makeStat(QStringLiteral("当前章心声"));
  statWarnings_ = makeStat(QStringLiteral("当前章校验"));
  statLayout->addStretch(1);
  root->addWidget(statBar);

  splitter_ = new QSplitter(Qt::Horizontal, this);
  splitter_->setContentsMargins(14, 12, 14, 14);
  splitter_->setHandleWidth(10);
  splitter_->setChildrenCollapsible(false);

  chaptersList_ = new QListWidget(splitter_);
  chaptersList_->setObjectName(QStringLiteral("dialogueChapters"));
  chaptersList_->setUniformItemSizes(true);
  connect(chaptersList_, &QListWidget::currentRowChanged, this,
          &DialoguePage::onChapterSelected);

  model_ = new SegmentTableModel(splitter_);
  tableView_ = new QTableView(splitter_);
  tableView_->setObjectName(QStringLiteral("dialogueTable"));
  tableView_->setModel(model_);
  tableView_->setSelectionBehavior(QAbstractItemView::SelectRows);
  tableView_->setSelectionMode(QAbstractItemView::SingleSelection);
  tableView_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tableView_->setAlternatingRowColors(false);
  tableView_->setShowGrid(false);
  tableView_->verticalHeader()->setVisible(false);
  tableView_->verticalHeader()->setDefaultSectionSize(28);

  auto* header = tableView_->horizontalHeader();
  header->setStretchLastSection(false);
  header->setHighlightSections(false);
  header->setMinimumSectionSize(40);
  header->setSectionResizeMode(SegmentTableModel::ColIndex, QHeaderView::Fixed);
  header->setSectionResizeMode(SegmentTableModel::ColType, QHeaderView::Fixed);
  header->setSectionResizeMode(SegmentTableModel::ColSpeaker,
                               QHeaderView::Interactive);
  header->setSectionResizeMode(SegmentTableModel::ColText, QHeaderView::Stretch);
  header->setSectionResizeMode(SegmentTableModel::ColConfidence,
                               QHeaderView::Fixed);
  tableView_->setColumnWidth(SegmentTableModel::ColIndex, 56);
  tableView_->setColumnWidth(SegmentTableModel::ColType, 64);
  tableView_->setColumnWidth(SegmentTableModel::ColSpeaker, 160);
  tableView_->setColumnWidth(SegmentTableModel::ColConfidence, 72);

  connect(tableView_->selectionModel(), &QItemSelectionModel::currentChanged,
          this, &DialoguePage::refreshDetail);

  detailEdit_ = new QTextEdit(splitter_);
  detailEdit_->setReadOnly(true);
  detailEdit_->setPlaceholderText(QStringLiteral("选中片段查看详情"));

  splitter_->addWidget(chaptersList_);
  splitter_->addWidget(tableView_);
  splitter_->addWidget(detailEdit_);
  splitter_->setStretchFactor(0, 8);   // 260px 量级
  splitter_->setStretchFactor(1, 22);  // 表格为主
  splitter_->setStretchFactor(2, 15);  // 详情
  root->addWidget(splitter_, 1);
}

void DialoguePage::openPipeline(const std::filesystem::path& path) {
  novel::TextLoadResult loaded;
  std::vector<novel::Chapter> chapters;
  std::vector<std::size_t> toc;
  {
    WaitCursor wait;
    loaded = novel::load_text_file(path);
    if (loaded.ok) {
      const novel::ParseResult parsed = novel::parse_chapters(loaded.text);
      chapters = std::move(parsed.chapters);
      toc = std::move(parsed.toc_lines);
    }
  }
  if (!loaded.ok) {
    showError(QStringLiteral("打开失败"), loaded.error);
    return;
  }

  // 一次性 resize 外层 vector，避免扩容导致 model 持有的指针失效。
  const std::size_t n = chapters.size();
  chapters_ = std::move(chapters);
  tocLines_ = std::move(toc);
  text_ = loaded.text;
  source_ = novel::path_to_utf8(path);
  encoding_ = loaded.encoding;
  currentPath_ = path;

  chapterSegments_.assign(n, {});
  chapterInfo_.assign(n, {});
  built_.assign(n, false);
  chapterMeta_.assign(n, {});
  for (std::size_t i = 0; i < n; ++i) {
    chapterMeta_[i].startLine = chapters_[i].start_line;
    chapterMeta_[i].endLine = chapters_[i].end_line;
  }

  currentChapter_ = -1;
  populateChapterList();
  detailEdit_->clear();
  reSplitBtn_->setEnabled(true);
  exportBtn_->setEnabled(true);
  fileLabel_->setText(toQString(novel::path_to_utf8(path.filename())));
  fileLabel_->setToolTip(toQString(novel::path_to_utf8(path)));

  refreshChapterStats();
  refreshStats();

  // 首屏：拆分第 1 章（用 WaitCursor 包裹），再显示，尽快出现列表与表格。
  if (n > 0) {
    WaitCursor wait;
    buildChapter(0);
    selectChapter(0);
  }
}

std::vector<std::string> DialoguePage::validateChapter(int i) const {
  if (i < 0 || static_cast<std::size_t>(i) >= chapterSegments_.size()) {
    return {};
  }
  novel::ScriptDocument document;
  document.source = source_;
  document.encoding = encoding_;
  document.characters.push_back(
      novel::ScriptCharacter{"narrator", "旁白", "", true, {}});
  if (static_cast<std::size_t>(i) < chapterInfo_.size()) {
    novel::ScriptChapter chapter = chapterInfo_[static_cast<std::size_t>(i)];
    chapter.index = 1;  // 单章校验文档里只有这一章
    document.chapters.push_back(std::move(chapter));
  }
  document.segments = chapterSegments_[static_cast<std::size_t>(i)];
  // 单章文档的章号必须归一，否则校验会按全书章号越界。
  for (novel::ScriptSegment& segment : document.segments) {
    segment.chapter_index = 1;
  }
  return novel::validate_script(document, text_);
}

void DialoguePage::buildChapter(int i) {
  if (i < 0 || static_cast<std::size_t>(i) >= chapters_.size()) return;
  if (built_[static_cast<std::size_t>(i)]) return;

  // 单章拆分；返回的 segments 的 chapter_index 改成 i+1。
  novel::ScriptDocument document = novel::build_script(
      text_, {chapters_[static_cast<std::size_t>(i)]}, tocLines_, source_,
      encoding_);
  if (!document.chapters.empty()) {
    chapterInfo_[static_cast<std::size_t>(i)] = document.chapters.front();
    chapterInfo_[static_cast<std::size_t>(i)].index = i + 1;
  }
  for (novel::ScriptSegment& segment : document.segments) {
    segment.chapter_index = i + 1;
  }
  const int slot = i;
  chapterSegments_[static_cast<std::size_t>(slot)] =
      std::move(document.segments);

  ChapterMeta meta;
  meta.startLine = chapterMeta_[static_cast<std::size_t>(i)].startLine;
  meta.endLine = chapterMeta_[static_cast<std::size_t>(i)].endLine;
  for (const novel::ScriptSegment& segment :
       chapterSegments_[static_cast<std::size_t>(i)]) {
    ++meta.segments;
    if (segment.type == novel::ScriptSegmentType::dialogue) {
      ++meta.dialogue;
    } else if (segment.type == novel::ScriptSegmentType::thought) {
      ++meta.thought;
    }
  }
  const std::vector<std::string> errors = validateChapter(i);
  meta.warnings = static_cast<int>(errors.size());
  chapterMeta_[static_cast<std::size_t>(i)] = std::move(meta);

  built_[static_cast<std::size_t>(i)] = true;
  // 更新列表项 tooltip 与文本（片段数/行范围）。
  if (chaptersList_ != nullptr &&
      i < chaptersList_->count()) {
    QListWidgetItem* item = chaptersList_->item(i);
    const ChapterMeta& m = chapterMeta_[static_cast<std::size_t>(i)];
    item->setText(QStringLiteral("%1. %2  （%3 片段）")
                     .arg(i + 1)
                     .arg(toQString(chapters_[static_cast<std::size_t>(i)].title))
                     .arg(m.segments));
    item->setToolTip(
        QStringLiteral("行 %1–%2，片段 %3，对白 %4，心声 %5，校验 %6")
            .arg(m.startLine)
            .arg(m.endLine)
            .arg(m.segments)
            .arg(m.dialogue)
            .arg(m.thought)
            .arg(m.warnings));
  }
}

void DialoguePage::populateChapterList() {
  chaptersList_->clear();
  for (std::size_t i = 0; i < chapters_.size(); ++i) {
    const ChapterMeta& m = chapterMeta_[static_cast<std::size_t>(i)];
    auto* item = new QListWidgetItem(
        QStringLiteral("%1. %2").arg(i + 1).arg(
            toQString(chapters_[i].title)),
        chaptersList_);
    item->setToolTip(QStringLiteral("行 %1–%2").arg(m.startLine).arg(m.endLine));
  }
}

void DialoguePage::selectChapter(int i) {
  if (i < 0 || static_cast<std::size_t>(i) >= chapters_.size()) return;
  if (i != currentChapter_) {
    buildChapter(i);
    currentChapter_ = i;
  }
  model_->setSegments(&chapterSegments_[static_cast<std::size_t>(i)]);
  tableView_->scrollToTop();
  detailEdit_->clear();
  refreshChapterStats();
  refreshStats();
  if (chaptersList_->currentRow() != i) chaptersList_->setCurrentRow(i);
}

void DialoguePage::onChapterSelected(int row) {
  // 列表点击切换章节；非用户触发的 currentRowChanged 已在 selectChapter 内同步跳过。
  if (row == currentChapter_) return;
  selectChapter(row);
}

void DialoguePage::refreshChapterStats() {
  if (currentChapter_ < 0) return;
  const ChapterMeta& meta = chapterMeta_[static_cast<std::size_t>(currentChapter_)];
  statSegments_->setText(QString::number(meta.segments));
  statDialogue_->setText(QString::number(meta.dialogue));
  statThought_->setText(QString::number(meta.thought));
  statWarnings_->setText(QString::number(meta.warnings));
}

void DialoguePage::refreshStats() {
  int built = 0;
  for (bool b : built_) {
    if (b) ++built;
  }
  statChapters_->setText(QString::number(static_cast<int>(chapters_.size())));
  statBuilt_->setText(
      QStringLiteral("%1/%2").arg(built).arg(chapters_.size()));
}

void DialoguePage::refreshDetail(const QModelIndex& current) {
  if (currentChapter_ < 0 || !current.isValid()) {
    detailEdit_->clear();
    return;
  }
  const std::vector<novel::ScriptSegment>& segments =
      chapterSegments_[static_cast<std::size_t>(currentChapter_)];
  if (static_cast<std::size_t>(current.row()) >= segments.size()) {
    detailEdit_->clear();
    return;
  }
  const novel::ScriptSegment& segment = segments[static_cast<std::size_t>(current.row())];

  QString rich;
  rich += QStringLiteral("<p><b>类型：</b>") + segmentTypeName(segment.type) +
          QStringLiteral("</p>");
  rich += QStringLiteral("<p><b>说话人提示：</b>") +
          (segment.speaker_hint.empty() ? QStringLiteral("—")
                                        : toQString(segment.speaker_hint).toHtmlEscaped()) +
          QStringLiteral("</p>");
  rich += QStringLiteral("<p><b>源偏移：</b>") +
          QString::number(static_cast<qlonglong>(segment.source_start)) +
          QStringLiteral(" - ") +
          QString::number(static_cast<qlonglong>(segment.source_end)) +
          QStringLiteral("</p>");
  rich += QStringLiteral("<p><b>原文：</b><br>") +
          toQString(segment.text).toHtmlEscaped() + QStringLiteral("</p>");
  rich += QStringLiteral("<p><b>朗读文本：</b><br>") +
          toQString(segment.speak_text).toHtmlEscaped() + QStringLiteral("</p>");
  detailEdit_->setHtml(rich);
}

void DialoguePage::openFile(const std::filesystem::path& path) {
  openPipeline(path);
}

void DialoguePage::onOpenClicked() {
  const QString path = QFileDialog::getOpenFileName(
      this, QStringLiteral("打开 TXT"), QString(),
      QStringLiteral("文本文件 (*.txt);;所有文件 (*.*)"));
  if (path.isEmpty()) return;
  openFile(novel::path_from_utf8(toStdString(path)));
}

void DialoguePage::onReSplitClicked() {
  if (currentChapter_ < 0) return;
  built_[static_cast<std::size_t>(currentChapter_)] = false;
  buildChapter(currentChapter_);
  selectChapter(currentChapter_);
}

void DialoguePage::onExportClicked() {
  if (chapters_.empty()) return;

  QString suggested = QStringLiteral("script.json");
  if (!source_.empty()) {
    const std::filesystem::path source = novel::path_from_utf8(source_);
    suggested =
        toQString(novel::path_to_utf8(source.stem())) + QStringLiteral(".script.json");
  }
  const QString path = QFileDialog::getSaveFileName(
      this, QStringLiteral("导出脚本"), suggested,
      QStringLiteral("JSON 文件 (*.json);;所有文件 (*.*)"));
  if (path.isEmpty()) return;

  // 逐章拆分（未拆分的），显示进度；组装完整文档后校验并写出。
  novel::ScriptDocument document;
  document.source = source_;
  document.encoding = encoding_;
  document.characters.push_back(
      novel::ScriptCharacter{"narrator", "旁白", "", true, {}});

  const int total = static_cast<int>(chapters_.size());
  {
    WaitCursor wait;
    for (int i = 0; i < total; ++i) {
      buildChapter(i);
      // 进度写入当前章统计位，必要时让界面刷新。
      statBuilt_->setText(QStringLiteral("%1/%2").arg(i + 1).arg(total));
      if (i % 8 == 0 || i == total - 1) {
        QApplication::processEvents();
      }
      if (static_cast<std::size_t>(i) < chapterInfo_.size()) {
        document.chapters.push_back(chapterInfo_[static_cast<std::size_t>(i)]);
      }
      const std::vector<novel::ScriptSegment>& segs =
          chapterSegments_[static_cast<std::size_t>(i)];
      document.segments.insert(document.segments.end(), segs.begin(), segs.end());
    }
  }

  const std::vector<std::string> errors = novel::validate_script(document, text_);
  if (!errors.empty()) {
    QString message = QStringLiteral("校验发现 %1 处问题，仍保留结果：\n\n")
                         .arg(errors.size());
    const std::size_t shown = std::min<std::size_t>(errors.size(), 8);
    for (std::size_t i = 0; i < shown; ++i) {
      message += QStringLiteral("· ") + toQString(errors[i]) + QLatin1Char('\n');
    }
    if (errors.size() > shown) message += QStringLiteral("…");
    QMessageBox::warning(this, QStringLiteral("拆分校验"), message);
  }

  const std::string json = novel::script_json(document);
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    showError(QStringLiteral("导出失败"), "无法写入文件：" + toStdString(path));
    return;
  }
  const qint64 written =
      file.write(json.data(), static_cast<qint64>(json.size()));
  if (written != static_cast<qint64>(json.size())) {
    file.cancelWriting();
    showError(QStringLiteral("导出失败"), "写入不完整：" + toStdString(path));
    return;
  }
  if (!file.commit()) {
    showError(QStringLiteral("导出失败"), "写入失败：" + toStdString(path));
    return;
  }
  QMessageBox::information(this, QStringLiteral("导出完成"),
                           QStringLiteral("已导出脚本到：\n") + path);
  refreshStats();
}

void DialoguePage::showError(const QString& title, const std::string& error) {
  QMessageBox::critical(this, title, toQString(error));
}

}  // namespace ui

#include "DialoguePage.moc"
