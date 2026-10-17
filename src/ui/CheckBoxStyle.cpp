#include "ui/CheckBoxStyle.hpp"

#include <QPainter>
#include <QStyleOption>

#include "ui/Theme.hpp"

namespace ui {

namespace {
// 在一个正方形区域内绘制对勾。tickColor 应与底色形成强对比。
void drawTick(QPainter* p, const QRect& box, const QColor& tickColor) {
  p->save();
  p->setRenderHint(QPainter::Antialiasing, true);
  p->setBrush(Qt::NoBrush);
  p->setPen(QPen(tickColor, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  const qreal s = box.width();
  const QPointF p1(box.x() + s * 0.26, box.y() + s * 0.55);
  const QPointF p2(box.x() + s * 0.44, box.y() + s * 0.72);
  const QPointF p3(box.x() + s * 0.76, box.y() + s * 0.30);
  p->drawLine(p1, p2);
  p->drawLine(p2, p3);
  p->restore();
}
}  // namespace

void CheckBoxStyle::drawPrimitive(PrimitiveElement element,
                                 const QStyleOption* opt, QPainter* p,
                                 const QWidget* w) const {
  if (element != PE_IndicatorCheckBox) {
    QProxyStyle::drawPrimitive(element, opt, p, w);
    return;
  }

  const auto* b = qstyleoption_cast<const QStyleOptionButton*>(opt);
  if (!b) {
    QProxyStyle::drawPrimitive(element, opt, p, w);
    return;
  }

  p->save();
  p->setRenderHint(QPainter::Antialiasing, true);

  // 居中一个正方形（按较小边），留出 1px 避免贴边。
  const QRect& r = b->rect;
  const int s = qMax(8, qMin(r.width(), r.height()) - 2);
  const QRect box(r.x() + (r.width() - s) / 2, r.y() + (r.height() - s) / 2, s,
                  s);

  const bool on = b->state & State_On;
  const bool hover = b->state & State_MouseOver;
  const bool focus = b->state & State_HasFocus;
  const bool enabled = b->state & State_Enabled;
  const qreal radius = 4.0;

  // 边框/底色选择：未勾选也要有足够对比度，不能只靠很浅的边框。
  QColor border = C.sub;
  if (on || hover || focus) border = C.accent;
  if (!enabled) border = C.border;

  if (on) {
    // 实心强调色底 + 高对比白色对勾（暗/亮都清晰）。
    QColor fill = C.accent;
    if (!enabled) fill = C.sub;
    p->setPen(Qt::NoPen);
    p->setBrush(fill);
    p->drawRoundedRect(box, radius, radius);
    drawTick(p, box, QColor(255, 255, 255));
  } else {
    // 未勾选：中性描边空框，底色与面板一致。
    p->setPen(QPen(border, 1.8));
    p->setBrush(enabled ? C.panel : C.bg);
    p->drawRoundedRect(box, radius, radius);
  }

  p->restore();
}

QRect CheckBoxStyle::subElementRect(SubElement subElement,
                                    const QStyleOption* opt,
                                    const QWidget* w) const {
  if (subElement == SE_CheckBoxIndicator) {
    // 固定 18x18 指示器，保证紧凑布局一致。
    QRect r = QProxyStyle::subElementRect(subElement, opt, w);
    const int s = 20;
    r.setSize(QSize(s, s));
    return r;
  }
  return QProxyStyle::subElementRect(subElement, opt, w);
}

int CheckBoxStyle::styleHint(StyleHint hint, const QStyleOption* opt,
                             const QWidget* w,
                             QStyleHintReturn* returnData) const {
  // 保留键盘/鼠标交互语义（点击、Space、焦点）。
  return QProxyStyle::styleHint(hint, opt, w, returnData);
}

}  // namespace ui
