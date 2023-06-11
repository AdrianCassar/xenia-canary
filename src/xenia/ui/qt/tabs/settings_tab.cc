#include "settings_tab.h"

#include <QButtonGroup>
#include <QGraphicsEffect>
#include <QScrollArea>

#include "xenia/ui/qt/settings/panes/advanced_pane.h"
#include "xenia/ui/qt/settings/panes/controls_pane.h"
#include "xenia/ui/qt/settings/panes/cpu_pane.h"
#include "xenia/ui/qt/settings/panes/general_pane.h"
#include "xenia/ui/qt/settings/panes/gpu_pane.h"
#include "xenia/ui/qt/settings/panes/interface_pane.h"
#include "xenia/ui/qt/settings/panes/library_pane.h"

#include "xenia/ui/qt/widgets/separator.h"

namespace xe {
namespace ui {
namespace qt {

SettingsTab::SettingsTab() : XTab("Settings", "SettingsTab") {
  settings_panes_ = {new GeneralPane(),   new CPUPane(),      new GPUPane(),
                     new InterfacePane(), new ControlsPane(), new LibraryPane(),
                     new AdvancedPane()};

  Build();
}

void SettingsTab::Build() {
  layout_ = new QHBoxLayout();
  layout_->setContentsMargins(0, 0, 0, 0);
  layout_->setSpacing(0);
  setLayout(layout_);

  content_widget_ = new QStackedWidget();

  for (SettingsPane* pane : settings_panes_) {
    pane->Build();
    content_widget_->addWidget(pane->widget());
  }

  BuildSidebar();

  QScrollArea* scroll_area = new QScrollArea(this);
  scroll_area->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

  scroll_area->setWidget(content_widget_);
  scroll_area->setWidgetResizable(true);

  layout_->addWidget(scroll_area);
}

void SettingsTab::BuildSidebar() {
  sidebar_container_ = new QWidget(this);
  sidebar_container_->setObjectName("sidebarContainer");

  QVBoxLayout* sidebar_layout = new QVBoxLayout;
  sidebar_layout->setContentsMargins(0, 0, 0, 0);
  sidebar_layout->setSpacing(0);

  sidebar_container_->setLayout(sidebar_layout);

  // Add drop shadow to sidebar widget
  QGraphicsDropShadowEffect* effect = new QGraphicsDropShadowEffect;
  effect->setBlurRadius(16);
  effect->setXOffset(4);
  effect->setYOffset(0);
  effect->setColor(QColor(0, 0, 0, 64));

  sidebar_container_->setGraphicsEffect(effect);

  // Create sidebar
  sidebar_ = new XSideBar;
  sidebar_->setOrientation(Qt::Vertical);
  sidebar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  sidebar_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);

  // Create sidebar title
  QWidget* sidebar_title = new QWidget;
  sidebar_title->setObjectName("sidebarTitle");

  QVBoxLayout* title_layout = new QVBoxLayout;
  title_layout->setContentsMargins(0, 40, 0, 0);
  title_layout->setSpacing(0);

  sidebar_title->setLayout(title_layout);

  // Title labels
  QLabel* xenia_title = new QLabel("Settings");
  xenia_title->setObjectName("sidebarTitleLabel");
  xenia_title->setAlignment(Qt::AlignCenter);
  title_layout->addWidget(xenia_title, 0, Qt::AlignCenter);

  // Title separator
  auto separator = new XSeparator();
  title_layout->addSpacing(32);
  title_layout->addWidget(separator, 0, Qt::AlignCenter);

  // Setup Sidebar toolbar
  sidebar_->addWidget(sidebar_title);

  sidebar_->addSpacing(20);

  QButtonGroup* bg = new QButtonGroup();

  // loop over sidebar button items and connect them to slots
  int counter = 0;
  for (auto it = settings_panes_.begin(); it != settings_panes_.end();
       ++it, ++counter) {
    SettingsPane* pane = *it;
    auto btn = sidebar_->addAction(pane->glyph(), pane->title());
    btn->setCheckable(true);
    bg->addButton(btn);

    // set the first item to checked
    if (counter == 0) {
      btn->setChecked(true);
    }

    // link up the clicked signal
    connect(btn, &XSideBarButton::clicked,
            [=]() { content_widget_->setCurrentIndex(counter); });
  }

  sidebar_layout->addWidget(sidebar_, 0, Qt::AlignHCenter | Qt::AlignTop);
  sidebar_layout->addStretch(1);

  // Add sidebar to tab widget
  layout_->addWidget(sidebar_container_, 0, Qt::AlignLeft);
}

}  // namespace qt
}  // namespace ui
}  // namespace xe