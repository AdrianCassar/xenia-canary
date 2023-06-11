#ifndef XENIA_UI_QT_TOOLBAR_ITEM_H_
#define XENIA_UI_QT_TOOLBAR_ITEM_H_

#include "xenia/ui/qt/actions/action.h"
#include "xenia/ui/qt/themeable_widget.h"

#include <QAction>
#include <QToolButton>

namespace xe {
namespace ui {
namespace qt {

class XToolBarItem : public Themeable<QToolButton> {
  Q_OBJECT

 public:
  explicit XToolBarItem(XAction* action, QWidget* parent = nullptr);

 protected:
  void enterEvent(QEnterEvent*) override;
  void mousePressEvent(QMouseEvent*) override;

 private:
  int spacing_ = 5;
  QString icon_char_;
  QColor icon_color_;
  QColor icon_color_active_;
  QFont icon_font_;
  QPixmap icon_pixmap_;
};

}  // namespace qt
}  // namespace ui
}  // namespace xe

#endif