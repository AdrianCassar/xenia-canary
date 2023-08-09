
#include "xenia/ui/qt/widgets/shell.h"

namespace xe {
namespace ui {
namespace qt {

XShell::XShell(QMainWindow* window) : Themeable<QWidget>("XShell") {
  window_ = window;
  Build();
}

void XShell::Build() {
  // Build Main Layout
  layout_ = new QVBoxLayout();
  layout_->setContentsMargins(0, 0, 0, 0);
  layout_->setSpacing(0);
  this->setLayout(layout_);

  BuildNav();
  BuildTabStack();
}

void XShell::BuildNav() {
  nav_ = new XNav();
  connect(nav_, SIGNAL(TabChanged(XTab*)), this, SLOT(TabChanged(XTab*)));
  layout_->addWidget(nav_, 0, Qt::AlignTop);
}

void XShell::BuildTabStack() {
  tab_stack_ = new QStackedLayout(window_);
  layout_->addLayout(tab_stack_);

  for (XTab* tab : nav_->tabs()) {
    tab_stack_->addWidget(tab);
  }
}

void XShell::TabChanged(XTab* tab) { tab_stack_->setCurrentWidget(tab); }

}  // namespace qt
}  // namespace ui
}  // namespace xe