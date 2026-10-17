#pragma once

#include <QProxyStyle>

namespace ui {

// 复选框自绘样式：用 QPainter 画圆角方块 + 高对比对勾，
// 颜色取自当前主题（ui::C）。避免 Qt5 QSS 不支持 data URI 导致对勾不可见。
class CheckBoxStyle : public QProxyStyle {
  Q_OBJECT
 public:
  explicit CheckBoxStyle(QStyle* base = nullptr) : QProxyStyle(base) {}

  void drawPrimitive(PrimitiveElement element, const QStyleOption* opt,
                     QPainter* p, const QWidget* w = nullptr) const override;

  QRect subElementRect(SubElement subElement, const QStyleOption* opt,
                       const QWidget* w) const override;

  int styleHint(StyleHint hint, const QStyleOption* opt = nullptr,
                const QWidget* w = nullptr,
                QStyleHintReturn* returnData = nullptr) const override;
};

}  // namespace ui
