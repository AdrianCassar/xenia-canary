#include "xenia/ui/qt/widgets/tab_selector.h"

#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>

namespace xe {
namespace ui {
namespace qt {

XTabSelector::XTabSelector() : Themeable<QWidget>("XTabSelector") {}

XTabSelector::XTabSelector(std::vector<XTab*> tabs)
    : Themeable<QWidget>("XTabSelector") {
  tabs_ = tabs;
  Build();
}

void XTabSelector::Build() {
  if (!needs_build_) {
    return;
  }

  if (tab_map_.size() > 0) {
    tab_map_.clear();
  }

  // Widget Measurements
  int selector_height = 0;
  int selector_width = 0;

  for (XTab* tab : tabs_) {
    QString tab_name = tab->tab_name();

    // Measure Tab Bounds
    font_.setPixelSize(font_size_);
    QFontMetrics metrics(font_);
    QRect rect = metrics.boundingRect(tab_name);
    int width = rect.width();
    int height = rect.height();

    // Translate rect to proper widget position
    rect.setCoords(selector_width, 0, selector_width + width, height);

    selector_height = height > selector_height ? height : selector_height;
    selector_width += width + tab_spacing_;
    tab_map_.insert(std::make_pair(tab, rect));
  }
  selector_width -= tab_spacing_;  // Remove spacing from last tab

  // Measure Tab Bar
  selector_height += bar_text_gap_;
  selector_height += bar_height_;

  setFixedSize(selector_width, selector_height);

  // Set default tab if we can
  if (!active_tab_ && tabs_.size()) {
    active_tab_ = tabs_[0];
    bar_rect_ = GetBarRect(active_tab_);
  }

  needs_build_ = false;
}

void XTabSelector::MoveBarRect(const QRectF& rect) {
  QPropertyAnimation* animation = new QPropertyAnimation(this, "bar_rect");
  animation->setDuration(bar_move_duration_);
  animation->setEndValue(rect);
  animation->setEasingCurve(QEasingCurve::InOutQuad);
  animation->start();
  connect(animation, &QPropertyAnimation::valueChanged, [=]() { update(); });
}

QRectF XTabSelector::GetBarRect(XTab* tab) {
  QRectF tab_rect = tab_map_[tab];
  const double width = tab_rect.width();
  const double margin = (width - (width * bar_ratio_)) / 2;

  const double x1 = tab_rect.x() + margin;
  const double x2 = tab_rect.x() + width - margin;
  QRectF bar_rect;
  bar_rect.setCoords(x1, height() - bar_height_, x2, height());

  return bar_rect;
}

void XTabSelector::SetTab(XTab* tab) {
  if (!tab || tab == active_tab_) {
    return;
  }

  active_tab_ = tab;
  MoveBarRect(GetBarRect(tab));
  emit TabChanged(tab);
}

void XTabSelector::SetTabIndex(int tab_index) {
  if (tab_index < tabs_.size()) {
    SetTab(tabs_.at(tab_index));
  }
}

void XTabSelector::mousePressEvent(QMouseEvent* event) {
  // Try to find a tab located inside the click
  XTab* clicked_tab = nullptr;
  for (auto tab : tab_map_) {
    QRectF rect = tab.second;
    if (rect.contains(event->pos())) {
      clicked_tab = tab.first;
      break;
    }
  }

  if (!clicked_tab || clicked_tab == active_tab_) {
    return;
  }

  SetTab(clicked_tab);
}

void XTabSelector::paintEvent(QPaintEvent*) {
  if (needs_build_) {
    Build();
  }

  QPainter painter(this);
  painter.setRenderHints(QPainter::TextAntialiasing | QPainter::Antialiasing);

  // Draw Text
  painter.setFont(font_);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QBrush(font_color_));
  for (auto tab : tab_map_) {
    QString tab_name = tab.first->tab_name();
    QRectF rect = tab.second;

    QFontMetrics measure(font_);
    QPointF text_baseline(rect.left(), rect.height() - measure.descent());

    QPainterPath text_path;
    text_path.addText(text_baseline, font_, tab_name);
    painter.drawPath(text_path);
  }

  // Draw Bar
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(bar_color_));
  painter.setBrush(QBrush(bar_color_));
  painter.drawRoundedRect(bar_rect_, 3, 3);
}

}  // namespace qt
}  // namespace ui
}  // namespace xe