#pragma once

#include <QTextEdit>

class QInputMethodEvent;
class QKeyEvent;
class QMimeData;

namespace ui {

// 预览正文控件：正文不可修改，但保留可见光标与当前行高亮，
// 供「在此拆分章节」定位。允许光标移动、Shift 选择、Ctrl+A/C 与鼠标拖选；
// 键入、退格/删除、回车、粘贴、拖放一律拦截。
class PreviewEdit : public QTextEdit {
 public:
  explicit PreviewEdit(QWidget* parent = nullptr);

  // 按当前主题重绘「光标所在行」底色。文档重建 / 主题切换后调用。
  void refreshLineHighlight();

 protected:
  void keyPressEvent(QKeyEvent* event) override;
  void inputMethodEvent(QInputMethodEvent* event) override;
  bool canInsertFromMimeData(const QMimeData* source) const override;
  void insertFromMimeData(const QMimeData* source) override;
};

}  // namespace ui
