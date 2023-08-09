#include "xenia/ui/qt/widgets/nav.h"

#include <QLabel>
#include "xenia/base/cvar.h"
#include "xenia/ui/qt/tabs/debug_tab.h"
#include "xenia/ui/qt/tabs/home_tab.h"
#include "xenia/ui/qt/tabs/library_tab.h"
#include "xenia/ui/qt/tabs/settings_tab.h"
#include "xenia/ui/qt/widgets/tab.h"

DECLARE_bool(show_debug_tab);

namespace xe {
namespace ui {
namespace qt {

XNav::XNav() : Themeable<QWidget>("XNav") { Build(); };

void XNav::Build() {
  // Build Main Layout
  layout_ = new QHBoxLayout();
  this->setLayout(layout_);

  // Build Components
  BuildXeniaIcon();
  BuildTabs();

  layout_->addStretch(1);
}

void XNav::BuildXeniaIcon() {
  xenia_icon_ = new QLabel();
  xenia_icon_->setFixedSize(40, 40);
  xenia_icon_->setScaledContents(true);
  xenia_icon_->setPixmap(QPixmap(":/resources/graphics/icon.ico"));

  QHBoxLayout* icon_layout = new QHBoxLayout();
  icon_layout->setContentsMargins(0, 0, 70, 0);
  icon_layout->addWidget(xenia_icon_, 0, Qt::AlignLeft);
  layout_->addLayout(icon_layout);
}

void XNav::BuildTabs() {
  // TODO(Wildenhaus): Define tabs in shell?
  // (Razzile): Probably better to move to main window
  // and keep widgets/ for reusable components
  std::vector<XTab*> tabs = {new HomeTab(), new LibraryTab(),
                             new SettingsTab()};

  if (cvars::show_debug_tab) {
    tabs.push_back(new DebugTab());
  }

  tab_selector_ = new XTabSelector(tabs);
  tab_selector_->setCursor(Qt::PointingHandCursor);
  layout_->addWidget(tab_selector_);

  connect(tab_selector_, SIGNAL(TabChanged(XTab*)), this,
          SIGNAL(TabChanged(XTab*)));
}

}  // namespace qt
}  // namespace ui
}  // namespace xe