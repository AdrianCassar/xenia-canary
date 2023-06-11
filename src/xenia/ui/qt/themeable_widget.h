#ifndef XENIA_UI_QT_THEMEABLEWIDGET_H_
#define XENIA_UI_QT_THEMEABLEWIDGET_H_

#include <QPainter>
#include <QStyleOption>
#include <QWidget>
#include "theme_manager.h"

#include <QtDebug>

namespace xe {
namespace ui {
namespace qt {

template <typename T>
class Themeable : public T {
 public:
  template <typename... Args>
  Themeable(QString name, Args&&... args) : T(args...) {
    static_assert(std::is_base_of<QWidget, T>::value,
                  "T is not derived from QWidget");

    ApplyTheme(name);
  }

  void ApplyTheme(const QString& theme_name) {
    if (!theme_name.isNull()) {
      reinterpret_cast<QWidget*>(this)->setObjectName(theme_name);
    }

    ThemeManager& manager = ThemeManager::Instance();
    Theme& theme = manager.current_theme();

    QString style = theme.StylesheetForComponent(theme_name);
    QString base_style = manager.base_style();
    if (!style.isNull()) {
      reinterpret_cast<QWidget*>(this)->setStyleSheet(base_style + style);
    }
  };

  virtual void paintEvent(QPaintEvent* event) override {
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    reinterpret_cast<QWidget*>(this)->style()->drawPrimitive(QStyle::PE_Widget,
                                                             &opt, &p, this);
    T::paintEvent(event);
  }
};
}  // namespace qt
}  // namespace ui
}  // namespace xe
#endif