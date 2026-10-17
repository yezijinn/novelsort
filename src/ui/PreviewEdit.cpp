#include "ui/PreviewEdit.hpp"

#include <QColor>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextFormat>

#include "ui/Theme.hpp"

namespace ui {
namespace {

// 光标移动 / 选择 / 焦点切换键：交给基类处理（Shift 组合同样放行）。
bool isNavigationKey(int key) {
  switch (key) {
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
    case Qt::Key_Select:
      return true;
    default:
      break;
  }
  return false;
}

}  // namespace

PreviewEdit::PreviewEdit(QWidget* parent) : QTextEdit(parent) {
  // 只读模式下 Qt 不绘制光标；改为「可编辑外观 + 输入全拦截」。
  setReadOnly(false);
  setCursorWidth(2);
  setAcceptDrops(false);
  setAcceptRichText(false);
  setUndoRedoEnabled(false);
  setTabChangesFocus(true);
  // 中文输入法同样不得写入正文。
  setAttribute(Qt::WA_InputMethodEnabled, false);

  setTextInteractionFlags(Qt::TextEditorInteraction);
  setFocusPolicy(Qt::StrongFocus);
  connect(this, &QTextEdit::cursorPositionChanged, this,
          [this]() { refreshLineHighlight(); });
  connect(this, &QTextEdit::selectionChanged, this,
          [this]() { refreshLineHighlight(); });
}

void PreviewEdit::refreshLineHighlight() {
  // 有选区时不要叠加整行高亮：否则浅色主题下选区会被行底色盖住，
  // 看起来像“拖选没有生效”。
  if (textCursor().hasSelection()) {
    setExtraSelections({});
    return;
  }
  QTextEdit::ExtraSelection sel;
  sel.cursor = textCursor();
  sel.cursor.clearSelection();
  QColor bg = C.accent;
  bg.setAlpha(g_dark ? 44 : 38);
  sel.format.setBackground(bg);
  // 背景铺满整行，不改变任何字符格式（无效内容删除线/颜色不受影响）。
  sel.format.setProperty(QTextFormat::FullWidthSelection, true);
  setExtraSelections({sel});
}

void PreviewEdit::keyPressEvent(QKeyEvent* event) {
  const int key = event->key();
  const Qt::KeyboardModifiers mods = event->modifiers();
  const bool ctrl = mods.testFlag(Qt::ControlModifier);
  const bool alt = mods.testFlag(Qt::AltModifier);
  const bool meta = mods.testFlag(Qt::MetaModifier);

  // 会改写正文的按键：退格 / 删除 / 回车 / Insert（覆盖模式）。
  if (key == Qt::Key_Backspace || key == Qt::Key_Delete ||
      key == Qt::Key_Enter || key == Qt::Key_Return ||
      key == Qt::Key_Insert) {
    event->accept();
    return;
  }

  if (ctrl && !alt && !meta) {
    switch (key) {
      case Qt::Key_A:      // 全选
      case Qt::Key_C:      // 复制
      case Qt::Key_Insert: // 复制
      case Qt::Key_Left:   // 按词移动 / 选择
      case Qt::Key_Right:
      case Qt::Key_Up:
      case Qt::Key_Down:
      case Qt::Key_Home:
      case Qt::Key_End:
      case Qt::Key_PageUp:
      case Qt::Key_PageDown:
        QTextEdit::keyPressEvent(event);
        return;
      default:  // Ctrl+V/X/Z/Y/B/I 等：不写正文、不改格式
        event->accept();
        return;
    }
  }

  if (alt || meta) {
    event->accept();
    return;
  }

  if (isNavigationKey(key) || key == Qt::Key_Escape ||
      (key >= Qt::Key_F1 && key <= Qt::Key_F35)) {
    QTextEdit::keyPressEvent(event);
    return;
  }

  // 可打印字符（含空格、数字、IME 直接投递的字符）与无文本的未知键：
  // 前者拦截；后者说明不产生输入（如 CapsLock），交给基类即可。
  if (!event->text().isEmpty()) {
    event->accept();
    return;
  }
  QTextEdit::keyPressEvent(event);
}

void PreviewEdit::inputMethodEvent(QInputMethodEvent* event) {
  // 丢弃预编辑与提交串，避免输入法写入正文。
  event->accept();
}

bool PreviewEdit::canInsertFromMimeData(const QMimeData* source) const {
  (void)source;
  return false;
}

void PreviewEdit::insertFromMimeData(const QMimeData* source) {
  (void)source;
}

}  // namespace ui
