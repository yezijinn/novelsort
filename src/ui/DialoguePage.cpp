#include "ui/DialoguePage.hpp"

#include <QAbstractItemView>
#include <QAbstractTableModel>
#include <QApplication>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
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
        return segment.speaker_hint.empty()
                   ? QStringLiteral("—")
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
  connect(openButton, &QPushButton::clicked, this,
          &DialoguePage::onOpenClicked);

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
  connect(exportBtn_, &QPushButton::clicked, this,
          &DialoguePage::onExportClicked);

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
  statChapters_ = makeStat(QStringLiteral("章节"));
  statSegments_ = makeStat(QStringLiteral("片段"));
  statDialogue_ = makeStat(QStringLiteral("对白"));
  statThought_ = makeStat(QStringLiteral("心声"));
  statWarnings_ = makeStat(QStringLiteral("校验警告"));
  statLayout->addStretch(1);
  root->addWidget(statBar);

  auto* splitter = new QSplitter(Qt::Horizontal, this);
  splitter->setContentsMargins(14, 12, 14, 14);
  splitter->setHandleWidth(10);
  splitter->setChildrenCollapsible(false);

  model_ = new SegmentTableModel(splitter);
  tableView_ = new QTableView(splitter);
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
  header->setSectionResizeMode(SegmentTableModel::ColIndex,
                               QHeaderView::Fixed);
  header->setSectionResizeMode(SegmentTableModel::ColType,
                               QHeaderView::Fixed);
  header->setSectionResizeMode(SegmentTableModel::ColSpeaker,
                               QHeaderView::Interactive);
  header->setSectionResizeMode(SegmentTableModel::ColText,
                               QHeaderView::Stretch);
  header->setSectionResizeMode(SegmentTableModel::ColConfidence,
                               QHeaderView::Fixed);
  tableView_->setColumnWidth(SegmentTableModel::ColIndex, 56);
  tableView_->setColumnWidth(SegmentTableModel::ColType, 64);
  tableView_->setColumnWidth(SegmentTableModel::ColSpeaker, 160);
  tableView_->setColumnWidth(SegmentTableModel::ColConfidence, 72);

  connect(tableView_->selectionModel(), &QItemSelectionModel::currentChanged,
          this, &DialoguePage::refreshDetail);

  detailEdit_ = new QTextEdit(splitter);
  detailEdit_->setReadOnly(true);
  detailEdit_->setPlaceholderText(QStringLiteral("选中片段查看详情"));

  splitter->addWidget(tableView_);
  splitter->addWidget(detailEdit_);
  splitter->setStretchFactor(0, 3);
  splitter->setStretchFactor(1, 2);
  root->addWidget(splitter, 1);
}

void DialoguePage::runPipeline(const std::filesystem::path& path) {
  novel::TextLoadResult loaded;
  novel::ParseResult parsed;
  {
    WaitCursor wait;
    loaded = novel::load_text_file(path);
    if (loaded.ok) {
      parsed = novel::parse_chapters(loaded.text);
      doc_ = novel::build_script(loaded.text, parsed.chapters,
                                 parsed.toc_lines, novel::path_to_utf8(path),
                                 loaded.encoding);
    }
  }
  if (!loaded.ok) {
    showError(QStringLiteral("打开失败"), loaded.error);
    return;
  }

  currentPath_ = path;
  validationWarnings_ = novel::validate_script(doc_, loaded.text);
  model_->setSegments(&doc_.segments);
  tableView_->scrollToTop();
  detailEdit_->clear();

  fileLabel_->setText(toQString(novel::path_to_utf8(path.filename())));
  fileLabel_->setToolTip(toQString(novel::path_to_utf8(path)));
  reSplitBtn_->setEnabled(true);
  exportBtn_->setEnabled(true);
  refreshStats();

  if (!validationWarnings_.empty()) {
    QString message =
        QStringLiteral("校验发现 %1 处问题，仍保留结果：\n\n")
            .arg(validationWarnings_.size());
    const std::size_t shown =
        std::min<std::size_t>(validationWarnings_.size(), 8);
    for (std::size_t i = 0; i < shown; ++i) {
      message += QStringLiteral("· ") + toQString(validationWarnings_[i]) +
                 QLatin1Char('\n');
    }
    if (validationWarnings_.size() > shown) message += QStringLiteral("…");
    QMessageBox::warning(this, QStringLiteral("拆分校验"), message);
  }
}

void DialoguePage::refreshStats() {
  int dialogue = 0;
  int thought = 0;
  for (const novel::ScriptSegment& segment : doc_.segments) {
    if (segment.type == novel::ScriptSegmentType::dialogue) {
      ++dialogue;
    } else if (segment.type == novel::ScriptSegmentType::thought) {
      ++thought;
    }
  }
  statChapters_->setText(
      QString::number(static_cast<int>(doc_.chapters.size())));
  statSegments_->setText(
      QString::number(static_cast<int>(doc_.segments.size())));
  statDialogue_->setText(QString::number(dialogue));
  statThought_->setText(QString::number(thought));
  statWarnings_->setText(
      QString::number(static_cast<int>(validationWarnings_.size())));
}

void DialoguePage::refreshDetail(const QModelIndex& current) {
  if (!current.isValid() ||
      static_cast<std::size_t>(current.row()) >= doc_.segments.size()) {
    detailEdit_->clear();
    return;
  }
  const novel::ScriptSegment& segment =
      doc_.segments[static_cast<std::size_t>(current.row())];

  QString rich;
  rich += QStringLiteral("<p><b>类型：</b>") + segmentTypeName(segment.type) +
          QStringLiteral("</p>");
  rich += QStringLiteral("<p><b>说话人提示：</b>") +
          (segment.speaker_hint.empty()
               ? QStringLiteral("—")
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
          toQString(segment.speak_text).toHtmlEscaped() +
          QStringLiteral("</p>");
  detailEdit_->setHtml(rich);
}

void DialoguePage::openFile(const std::filesystem::path& path) {
  runPipeline(path);
}

void DialoguePage::onOpenClicked() {
  const QString path = QFileDialog::getOpenFileName(
      this, QStringLiteral("打开 TXT"), QString(),
      QStringLiteral("文本文件 (*.txt);;所有文件 (*.*)"));
  if (path.isEmpty()) return;
  runPipeline(novel::path_from_utf8(toStdString(path)));
}

void DialoguePage::onReSplitClicked() {
  if (currentPath_.empty()) return;
  runPipeline(currentPath_);
}

void DialoguePage::onExportClicked() {
  if (doc_.segments.empty()) return;

  QString suggested = QStringLiteral("script.json");
  if (!doc_.source.empty()) {
    const std::filesystem::path source =
        novel::path_from_utf8(doc_.source);
    suggested = toQString(novel::path_to_utf8(source.stem())) +
                QStringLiteral(".script.json");
  }
  const QString path = QFileDialog::getSaveFileName(
      this, QStringLiteral("导出脚本"), suggested,
      QStringLiteral("JSON 文件 (*.json);;所有文件 (*.*)"));
  if (path.isEmpty()) return;

  const std::string json = novel::script_json(doc_);
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    showError(QStringLiteral("导出失败"),
              "无法写入文件：" + toStdString(path));
    return;
  }
  const qint64 written =
      file.write(json.data(), static_cast<qint64>(json.size()));
  if (written != static_cast<qint64>(json.size())) {
    file.cancelWriting();
    showError(QStringLiteral("导出失败"),
              "写入不完整：" + toStdString(path));
    return;
  }
  if (!file.commit()) {
    showError(QStringLiteral("导出失败"),
              "写入失败：" + toStdString(path));
    return;
  }
  QMessageBox::information(this, QStringLiteral("导出完成"),
                           QStringLiteral("已导出脚本到：\n") + path);
}

void DialoguePage::showError(const QString& title,
                             const std::string& error) {
  QMessageBox::critical(this, title, toQString(error));
}

}  // namespace ui

#include "DialoguePage.moc"
