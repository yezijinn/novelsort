#include "ui/ChapterDialogs.hpp"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/CheckBoxStyle.hpp"
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
#endif

namespace ui {
namespace {

// 对话框骨架：自绘深色标题栏 + 标题 + 垂直布局。
QDialog* makeDialog(QWidget* parent, bool dark, const QString& title,
                    QVBoxLayout** outLay) {
  QDialog* dlg = new QDialog(parent);
  dlg->setWindowTitle(title);
  dlg->setMinimumWidth(metrics::kDialogMinWidth);
  dlg->setObjectName(QStringLiteral("dialog"));
#ifdef _WIN32
  applyWindowsFrame(dlg->winId(), dark);
#endif
  QVBoxLayout* lay = new QVBoxLayout(dlg);
  lay->setContentsMargins(20, 18, 20, 16);
  lay->setSpacing(12);

  QLabel* head = new QLabel(title, dlg);
  head->setObjectName(QStringLiteral("dialogTitle"));
  lay->addWidget(head);
  *outLay = lay;
  return dlg;
}

// 一行「标签 + 控件」。
QHBoxLayout* fieldRow(QWidget* parent, QVBoxLayout* lay, const QString& label,
                      QWidget* field, int labelWidth) {
  QHBoxLayout* row = new QHBoxLayout();
  row->setSpacing(8);
  QLabel* lab = new QLabel(label, parent);
  lab->setFixedWidth(labelWidth);
  lab->setObjectName(QStringLiteral("dialogHint"));
  row->addWidget(lab);
  row->addWidget(field, 1);
  lay->addLayout(row);
  return row;
}

// 确定 / 取消按钮组；outOk 返回确定按钮，供调用方控制可用态。
QDialogButtonBox* addButtons(QDialog* dlg, QVBoxLayout* lay,
                             QPushButton** outOk) {
  QPushButton* ok = new QPushButton(QStringLiteral("确定"), dlg);
  ok->setProperty("class", "primary");
  ok->setCursor(Qt::PointingHandCursor);
  QPushButton* cancel = new QPushButton(QStringLiteral("取消"), dlg);
  cancel->setCursor(Qt::PointingHandCursor);
  QDialogButtonBox* box = new QDialogButtonBox(dlg);
  box->addButton(ok, QDialogButtonBox::AcceptRole);
  box->addButton(cancel, QDialogButtonBox::RejectRole);
  lay->addWidget(box);
  QObject::connect(box, &QDialogButtonBox::accepted, dlg, &QDialog::accept);
  QObject::connect(box, &QDialogButtonBox::rejected, dlg, &QDialog::reject);
  *outOk = ok;
  return box;
}

QString chapterColumnText(const std::string& value) {
  return QString::fromStdString(value);
}

}  // namespace

QDialog* buildRenameDialog(QWidget* parent, bool dark,
                           const novel::Chapter& chapter,
                           RenameFields& fields) {
  QVBoxLayout* lay = nullptr;
  QDialog* dlg =
      makeDialog(parent, dark, QStringLiteral("重命名章节"), &lay);

  QLabel* hint = new QLabel(
      QStringLiteral("序号用于识别可递增编号（如「第1章」）；标题为序号之后的文字。"
                     "两者至少填一项，序号可留空。"),
      dlg);
  hint->setObjectName(QStringLiteral("dialogHint"));
  hint->setWordWrap(true);
  lay->addWidget(hint);

  QLineEdit* numEdit = new QLineEdit(dlg);
  numEdit->setText(chapterColumnText(chapter.number));
  numEdit->setPlaceholderText(QStringLiteral("如：第1章 / 序章（可留空）"));
  fields.number = numEdit;

  QLineEdit* nameEdit = new QLineEdit(dlg);
  nameEdit->setText(chapterColumnText(chapter.name));
  nameEdit->setPlaceholderText(QStringLiteral("如：启程（可留空）"));
  fields.name = nameEdit;

  fieldRow(dlg, lay, QStringLiteral("序号"), numEdit,
           metrics::kFieldLabelWidth);
  fieldRow(dlg, lay, QStringLiteral("标题"), nameEdit,
           metrics::kFieldLabelWidth);

  QPushButton* ok = nullptr;
  addButtons(dlg, lay, &ok);
  const auto syncOk = [ok, numEdit, nameEdit]() {
    ok->setEnabled(!numEdit->text().trimmed().isEmpty() ||
                   !nameEdit->text().trimmed().isEmpty());
  };
  QObject::connect(numEdit, &QLineEdit::textChanged, dlg, syncOk);
  QObject::connect(nameEdit, &QLineEdit::textChanged, dlg, syncOk);
  syncOk();

  if (chapter.number.empty()) {
    nameEdit->setFocus();
    nameEdit->selectAll();
  } else {
    numEdit->setFocus();
    numEdit->selectAll();
  }
  return dlg;
}

QDialog* buildSplitDialog(QWidget* parent, bool dark,
                          const novel::Chapter& chapter,
                          const QString& suggestedNumber,
                          SplitFields& fields) {
  fields.autoNumber = chapter.numbered && !chapter.excluded_from_numbering;
  QVBoxLayout* lay = nullptr;
  QDialog* dlg =
      makeDialog(parent, dark, QStringLiteral("在此拆分章节"), &lay);

  QLabel* hint = new QLabel(
      fields.autoNumber
          ? QStringLiteral("从光标所在行起划入新章。当前章有编号，新章序号自动按"
                           "当前序号 +1 生成，后续章节依次后移。")
          : QStringLiteral("从光标所在行起划入新章。当前章没有可递增序号；"
                           "可勾选「新章节参与排序」并填写序号，后续章节自动后移；"
                           "不勾选则新章不参与排序。"),
      dlg);
  hint->setObjectName(QStringLiteral("dialogHint"));
  hint->setWordWrap(true);
  lay->addWidget(hint);

  QLineEdit* numEdit = new QLineEdit(dlg);
  if (fields.autoNumber) {
    numEdit->setText(QStringLiteral("自动：当前序号 +1"));
    numEdit->setReadOnly(true);
  } else {
    numEdit->setText(suggestedNumber);
    numEdit->setPlaceholderText(QStringLiteral("如：第1章"));
  }
  fields.number = numEdit;

  QLineEdit* nameEdit = new QLineEdit(dlg);
  nameEdit->setPlaceholderText(QStringLiteral("新章节标题"));
  fields.name = nameEdit;

  fieldRow(dlg, lay, QStringLiteral("序号"), numEdit,
           metrics::kFieldLabelWidth);
  fieldRow(dlg, lay, QStringLiteral("标题"), nameEdit,
           metrics::kFieldLabelWidth);

  QCheckBox* participate = nullptr;
  if (!fields.autoNumber) {
    participate = new QCheckBox(QStringLiteral("新章节参与排序"), dlg);
    participate->setStyle(new ui::CheckBoxStyle(participate->style()));
    // 有可用的后续编号时默认参与；否则默认不参与，由用户主动勾选。
    participate->setChecked(!suggestedNumber.isEmpty());
    participate->setToolTip(
        QStringLiteral("勾选后新章占用序号，后续参与排序的章节自动 +1"));
    lay->addWidget(participate);
    fields.participate = participate;
  }

  QPushButton* ok = nullptr;
  addButtons(dlg, lay, &ok);
  const auto syncOk = [ok, numEdit, nameEdit, participate,
                       autoNumber = fields.autoNumber]() {
    if (autoNumber) {
      ok->setEnabled(!nameEdit->text().trimmed().isEmpty());
    } else {
      const bool wantNumber = participate != nullptr && participate->isChecked();
      numEdit->setEnabled(wantNumber);
      ok->setEnabled(!nameEdit->text().trimmed().isEmpty() &&
                     (!wantNumber || !numEdit->text().trimmed().isEmpty()));
    }
  };
  QObject::connect(numEdit, &QLineEdit::textChanged, dlg, syncOk);
  QObject::connect(nameEdit, &QLineEdit::textChanged, dlg, syncOk);
  if (participate) {
    QObject::connect(participate, &QCheckBox::toggled, dlg, syncOk);
  }
  syncOk();

  nameEdit->setFocus();
  return dlg;
}

QDialog* buildRenumberDialog(QWidget* parent, bool dark,
                             RenumberFields& fields) {
  QVBoxLayout* lay = nullptr;
  QDialog* dlg =
      makeDialog(parent, dark, QStringLiteral("重新编排章节"), &lay);

  QLabel* tip = new QLabel(
      QStringLiteral("只重写能解析出编号的标题。「不参与排序」与序章 / 番外等"
                     "无编号章节不计数，原样保留。"),
      dlg);
  tip->setObjectName(QStringLiteral("dialogHint"));
  tip->setWordWrap(true);
  lay->addWidget(tip);

  QComboBox* styleCombo = new QComboBox(dlg);
  styleCombo->addItem(QStringLiteral("阿拉伯数字"), false);
  styleCombo->addItem(QStringLiteral("中文数字"), true);
  styleCombo->setCurrentIndex(0);
  fields.style = styleCombo;

  QSpinBox* startSpin = new QSpinBox(dlg);
  startSpin->setRange(1, 9999);
  startSpin->setValue(1);
  fields.start = startSpin;

  QComboBox* padCombo = new QComboBox(dlg);
  padCombo->addItem(QStringLiteral("自然"), 0);
  padCombo->addItem(QStringLiteral("2 位（01）"), 2);
  padCombo->addItem(QStringLiteral("3 位（001）"), 3);
  padCombo->addItem(QStringLiteral("4 位（0001）"), 4);
  padCombo->setCurrentIndex(0);
  fields.pad = padCombo;

  const auto labelled = [&](const QString& text, QWidget* w) {
    fieldRow(dlg, lay, text, w, metrics::kDialogLabelWidth);
  };
  labelled(QStringLiteral("编号写法："), styleCombo);
  labelled(QStringLiteral("起始号："), startSpin);
  labelled(QStringLiteral("序号位数："), padCombo);

  QObject::connect(styleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                   dlg, [styleCombo, padCombo](int) {
                     const bool chinese = styleCombo->currentData().toBool();
                     padCombo->setEnabled(!chinese);  // 中文数字无补零概念
                     if (chinese) padCombo->setCurrentIndex(0);
                   });

  QPushButton* ok = nullptr;
  addButtons(dlg, lay, &ok);
  return dlg;
}

}  // namespace ui
