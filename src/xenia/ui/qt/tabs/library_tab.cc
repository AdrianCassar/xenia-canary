#include "xenia/ui/qt/tabs/library_tab.h"
#include "xenia/ui/qt/actions/add_game.h"
#include "xenia/ui/qt/widgets/slider.h"

namespace xe {
namespace ui {
namespace qt {

LibraryTab::LibraryTab() : XTab("Library") {
  Build();
  return;
}

void LibraryTab::Build() {
  layout_ = new QVBoxLayout();
  layout_->setContentsMargins(0, 0, 0, 0);
  layout_->setSpacing(0);
  setLayout(layout_);

  BuildToolBar();
  BuildListView();
  connect(slider_, SIGNAL(valueChanged(int)), list_view_,
          SLOT(setRowSize(int)));
}

void LibraryTab::BuildToolBar() {
  toolbar_ = new XToolBar(this);
  toolbar_->setFixedHeight(46);
  layout_->addWidget(toolbar_);

  toolbar_->addAction(new XAddGameAction());
  toolbar_->addAction(new XAction(QChar(0xF12B), "Add Folder"));

  toolbar_->addSeparator();

  toolbar_->addAction(new XAction(QChar(0xEDB5), "Play"));
  toolbar_->addAction(new XAction(QChar(0xEBE8), "Debug"));
  toolbar_->addAction(new XAction(QChar(0xE946), "Info"));

  toolbar_->addSeparator();

  toolbar_->addAction(new XAction(QChar(0xE8FD), "List"));
  toolbar_->addAction(new XAction(QChar(0xF0E2), "Grid"));

  slider_ = new XSlider(Qt::Horizontal, this);
  slider_->setRange(48, 96);
  slider_->setFixedWidth(100);
  toolbar_->addWidget(slider_);
}

void LibraryTab::BuildListView() {
  list_view_ = new XGameListView(this);
  layout_->addWidget(list_view_);
}

}  // namespace qt
}  // namespace ui
}  // namespace xe