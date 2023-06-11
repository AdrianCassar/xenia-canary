#include "xenia/ui/qt/tabs/debug_tab.h"

#include <QButtonGroup>
#include <QGraphicsEffect>
#include <QHBoxLayout>
#include <QMenu>

#include "xenia/ui/qt/widgets/checkbox.h"
#include "xenia/ui/qt/widgets/combobox.h"
#include "xenia/ui/qt/widgets/dropdown_button.h"
#include "xenia/ui/qt/widgets/groupbox.h"
#include "xenia/ui/qt/widgets/line_edit.h"
#include "xenia/ui/qt/widgets/push_button.h"
#include "xenia/ui/qt/widgets/radio_button.h"
#include "xenia/ui/qt/widgets/scroll_area.h"
#include "xenia/ui/qt/widgets/separator.h"
#include "xenia/ui/qt/widgets/slider.h"
#include "xenia/ui/qt/widgets/tab_selector.h"
#include "xenia/ui/qt/widgets/text_edit.h"

namespace xe {
namespace ui {
namespace qt {

// sidebar_->addAction(0xE90F, "Components");
// sidebar_->addAction(0xE700, "Navigation");
// sidebar_->addAction(0xE790, "Theme");
// sidebar_->addAction(0xE8F1, "Library");

DebugTab::DebugTab() : XTab("Debug", "DebugTab") {
  sidebar_items_ =
      QList<SidebarItem>{{QChar(0xE90F), "Components", CreateComponentsTab()},
                         {QChar(0xE700), "Navigation", CreateNavigationTab()},
                         {QChar(0xE790), "Theme", CreateThemeTab()},
                         {QChar(0xE8F1), "Library", CreateLibraryTab()}};

  Build();
}

void DebugTab::Build() {
  layout_ = new QHBoxLayout();
  layout_->setContentsMargins(0, 0, 0, 0);
  layout_->setSpacing(0);
  setLayout(layout_);

  BuildSidebar();

  content_widget_ = new QStackedWidget();

  for (const SidebarItem& item : sidebar_items_) {
    content_widget_->addWidget(item.widget);
  }

  layout_->addWidget(content_widget_);
}

void DebugTab::BuildSidebar() {
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
  QLabel* xenia_title = new QLabel("Debug");
  xenia_title->setObjectName("sidebarTitleLabel");
  xenia_title->setAlignment(Qt::AlignCenter);
  title_layout->addWidget(xenia_title, 0, Qt::AlignCenter);

  // Title separator
  auto separator = new XSeparator;
  title_layout->addSpacing(32);
  title_layout->addWidget(separator, 0, Qt::AlignCenter);

  // Setup Sidebar toolbar
  sidebar_->addWidget(sidebar_title);

  sidebar_->addSpacing(20);

  QButtonGroup* bg = new QButtonGroup();

  // loop over sidebar button items and connect them to slots
  int counter = 0;
  for (auto it = sidebar_items_.begin(); it != sidebar_items_.end();
       ++it, ++counter) {
    SidebarItem& item = *it;
    auto btn = sidebar_->addAction(item.glyph, item.name);
    btn->setCheckable(true);
    bg->addButton(btn);

    // set the first item to checked
    if (counter == 0) {
      btn->setChecked(true);
    }

    // link up the clicked signal
    connect(btn, &XSideBarButton::clicked,
            [&]() { content_widget_->setCurrentWidget(item.widget); });
  }

  sidebar_layout->addWidget(sidebar_, 0, Qt::AlignHCenter | Qt::AlignTop);
  sidebar_layout->addStretch(1);

  // Add sidebar to tab widget
  layout_->addWidget(sidebar_container_, 0, Qt::AlignLeft);
}

QWidget* DebugTab::CreateComponentsTab() {
  QWidget* w = new QWidget();
  w->setSizePolicy(QSizePolicy::MinimumExpanding,
                   QSizePolicy::MinimumExpanding);

  XScrollArea* scroll_area = new XScrollArea(this);
  scroll_area->setWidget(w);

  QVBoxLayout* layout = new QVBoxLayout();
  w->setLayout(layout);

  layout->setSpacing(16);
  layout->setContentsMargins(0, 16, 0, 0);

  layout->addWidget(CreateButtonGroup());
  layout->addWidget(CreateSliderGroup());
  layout->addWidget(CreateCheckboxGroup());
  layout->addWidget(CreateRadioButtonGroup());
  layout->addWidget(CreateInputGroup());

  layout->addStretch();

  return scroll_area;
}
QWidget* DebugTab::CreateNavigationTab() {
  QWidget* w = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout(w);
  layout->setSpacing(0);
  layout->setContentsMargins(64, 64, 64, 64);

  QWidget* container = new QWidget();
  container->setObjectName("navigationContainer");
  container->setFixedSize(640, 480);
  QVBoxLayout* container_layout = new QVBoxLayout(container);
  container_layout->setSpacing(4);
  container_layout->setContentsMargins(0, 0, 0, 0);
  container->setLayout(container_layout);

  XTab *tab1 = new XTab("Tab1"), *tab2 = new XTab("Tab2"),
       *tab3 = new XTab("Tab3");

  QVBoxLayout *tab1_layout = new QVBoxLayout(tab1),
              *tab2_layout = new QVBoxLayout(tab2),
              *tab3_layout = new QVBoxLayout(tab3);

  std::vector<XTab*> tabs{tab1, tab2, tab3};

  XTabSelector* tab_selector = new XTabSelector(tabs);
  container_layout->addWidget(tab_selector, 0, Qt::AlignCenter);

  QStackedLayout* content_layout = new QStackedLayout();
  content_layout->setSpacing(0);
  content_layout->setContentsMargins(0, 0, 0, 0);

  container_layout->addLayout(content_layout);

  tab1_layout->addWidget(CreateTab1Widget(tab_selector, content_layout));

  QWidget* test2 = new QWidget();
  test2->setStyleSheet("background:blue");

  tab2_layout->addWidget(test2);

  QWidget* test3 = new QWidget();
  test3->setStyleSheet("background:green");

  tab3_layout->addWidget(test3);

  for (XTab* tab : tabs) {
    QVBoxLayout* tab_layout = qobject_cast<QVBoxLayout*>(tab->layout());
    tab_layout->setContentsMargins(0, 0, 0, 0);
    tab_layout->setSpacing(0);
    content_layout->addWidget(tab);
  }
  content_layout->setCurrentWidget(tab1);

  connect(tab_selector, &XTabSelector::TabChanged, [content_layout](XTab* tab) {
    content_layout->setCurrentWidget(tab);
  });

  layout->addWidget(container, 0, Qt::AlignHCenter | Qt::AlignTop);
  return w;
}
QWidget* DebugTab::CreateThemeTab() {
  QWidget* w = new QWidget();
  w->setStyleSheet("background: #505050");
  QVBoxLayout* layout = new QVBoxLayout(w);
  layout->setContentsMargins(64, 64, 64, 64);
  ThemeManager& theme_manager = ThemeManager::Instance();
  Theme& theme = theme_manager.current_theme();

  QLabel* label = new QLabel(
      QStringLiteral("Current Theme: %1\n Description: %2\n Path: %3")
          .arg(theme.config().name(), theme.config().description(),
               theme.directory()));

  layout->addWidget(label);

  return w;
}
QWidget* DebugTab::CreateLibraryTab() {
  QWidget* w = new QWidget();
  w->setStyleSheet("background: yellow;");
  return w;
}

QWidget* DebugTab::CreateButtonGroup() {
  QWidget* group = new QWidget();

  QVBoxLayout* group_layout = new QVBoxLayout();
  group_layout->setContentsMargins(32, 0, 32, 0);
  group_layout->setSpacing(16);
  group->setLayout(group_layout);

  XGroupBox* groupbox = new XGroupBox("Buttons");

  QVBoxLayout* groupbox_layout = new QVBoxLayout();
  groupbox_layout->setContentsMargins(16, 16, 16, 16);
  groupbox->setLayout(groupbox_layout);

  group_layout->addWidget(groupbox);

  QLabel* pushbtn_label = new QLabel("Push Buttons");
  groupbox_layout->addWidget(pushbtn_label);

  QHBoxLayout* pushbtn_layout = new QHBoxLayout();
  pushbtn_layout->setSpacing(32);
  pushbtn_layout->setContentsMargins(0, 0, 0, 0);

  QSize btn_size(120, 40);

  XPushButton* pushbtn1 = new XPushButton("Push Button");
  pushbtn1->setMinimumSize(btn_size);
  XPushButton* pushbtn2 = new XPushButton("Disabled");
  pushbtn2->setDisabled(true);
  pushbtn2->setMinimumSize(btn_size);

  pushbtn_layout->addWidget(pushbtn1);
  pushbtn_layout->addWidget(pushbtn2);

  pushbtn_layout->addStretch();

  groupbox_layout->addLayout(pushbtn_layout);

  QLabel* dropdown_btn_label = new QLabel("Dropdown Buttons");
  groupbox_layout->addWidget(dropdown_btn_label);

  QHBoxLayout* dropdown_btn_layout = new QHBoxLayout();
  dropdown_btn_layout->setSpacing(32);
  dropdown_btn_layout->setContentsMargins(0, 0, 0, 0);

  QMenu* menu = new QMenu(this);
  menu->addAction("Test");
  menu->addSeparator();
  menu->addAction("Close");

  XDropdownButton* dropdown_btn = new XDropdownButton("Dropdown Button", menu);
  dropdown_btn->setMinimumSize(btn_size);

  XDropdownButton* dropdown_btn_disabled = new XDropdownButton("Disabled");
  dropdown_btn_disabled->setDisabled(true);
  dropdown_btn_disabled->setMinimumSize(btn_size);

  dropdown_btn_layout->addWidget(dropdown_btn);
  dropdown_btn_layout->addWidget(dropdown_btn_disabled);

  dropdown_btn_layout->addStretch();

  groupbox_layout->addLayout(dropdown_btn_layout);

  QLabel* combobox_label = new QLabel("Combo Boxes");
  groupbox_layout->addWidget(combobox_label);

  QHBoxLayout* combobox_layout = new QHBoxLayout();
  combobox_layout->setSpacing(32);
  combobox_layout->setContentsMargins(0, 0, 0, 0);

  QSize combobox_size = QSize(120, 32);

  XComboBox* combobox1 = new XComboBox();
  combobox1->setMinimumSize(combobox_size);

  combobox1->addItem("Simple String 1");
  combobox1->addItem("Simple String 2");

  combobox_layout->addWidget(combobox1);

  XComboBox* combobox2 = new XComboBox();
  combobox2->setMinimumSize(combobox_size);

  QAction* action1 = new QAction("Action 1");
  QAction* action2 = new QAction("Action 2");

  combobox2->addItem(action1->text(), QVariant::fromValue(action1));
  combobox2->addItem(action2->text(), QVariant::fromValue(action2));

  combobox2->addAction(action2);
  combobox_layout->addWidget(combobox2);

  combobox_layout->addStretch();

  groupbox_layout->addLayout(combobox_layout);

  return group;
}

QWidget* DebugTab::CreateSliderGroup() {
  QWidget* group = new QWidget();
  QVBoxLayout* group_layout = new QVBoxLayout();
  group_layout->setContentsMargins(32, 0, 32, 0);
  group_layout->setSpacing(16);
  group->setLayout(group_layout);

  XGroupBox* groupbox = new XGroupBox("Sliders");

  QHBoxLayout* groupbox_layout = new QHBoxLayout();
  groupbox_layout->setContentsMargins(16, 16, 16, 16);
  groupbox->setLayout(groupbox_layout);

  group_layout->addWidget(groupbox);

  // horizontal slider

  XSlider* horizontal_slider = new XSlider();
  horizontal_slider->setFixedWidth(120);

  QLabel* horizontal_label = new QLabel();
  horizontal_label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
  connect(horizontal_slider, &XSlider::valueChanged, [=](int value) {
    QString text;
    horizontal_label->setText(text.asprintf("Value: %02d", value));
  });
  horizontal_slider->valueChanged(0);

  groupbox_layout->addWidget(horizontal_slider);
  groupbox_layout->addWidget(horizontal_label);

  groupbox_layout->addSpacing(16);

  // vertical slider

  XSlider* vertical_slider = new XSlider(Qt::Vertical);
  vertical_slider->setFixedSize(20, 60);
  // vertical slider causes issues in a vertical orientation right now
  // TODO: fix this. for now just ignore its vertical size
  vertical_slider->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);

  QLabel* vertical_label = new QLabel();
  connect(vertical_slider, &XSlider::valueChanged, [=](int value) {
    QString text;
    vertical_label->setText(text.asprintf("Value: %02d", value));
  });
  vertical_slider->valueChanged(0);

  groupbox_layout->addWidget(vertical_slider);
  groupbox_layout->addWidget(vertical_label);

  groupbox_layout->addStretch();

  return group;
}

QWidget* DebugTab::CreateCheckboxGroup() {
  QWidget* group = new QWidget();

  QVBoxLayout* group_layout = new QVBoxLayout();
  group_layout->setContentsMargins(32, 0, 32, 0);
  group_layout->setSpacing(16);
  group->setLayout(group_layout);

  XGroupBox* groupbox = new XGroupBox("Checkboxes");

  QVBoxLayout* groupbox_layout = new QVBoxLayout();
  groupbox_layout->setContentsMargins(16, 16, 16, 16);
  groupbox->setLayout(groupbox_layout);

  group_layout->addWidget(groupbox);

  QHBoxLayout* layer_1_layout = new QHBoxLayout();
  layer_1_layout->setContentsMargins(0, 0, 0, 0);
  layer_1_layout->setSpacing(20);

  QHBoxLayout* layer_2_layout = new QHBoxLayout();
  layer_2_layout->setContentsMargins(0, 0, 0, 0);
  layer_2_layout->setSpacing(20);

  groupbox_layout->addLayout(layer_1_layout);
  groupbox_layout->addLayout(layer_2_layout);

  group_layout->addLayout(groupbox_layout);

  XCheckBox* checkbox1 = new XCheckBox();
  checkbox1->setText("Test Checkbox");

  XCheckBox* checkbox2 = new XCheckBox();
  checkbox2->set_custom(true);
  checkbox2->set_checked_color(QColor(255, 150, 100));
  checkbox2->setText("Alternate Color");

  layer_1_layout->addWidget(checkbox1);
  layer_1_layout->addWidget(checkbox2);

  layer_1_layout->addStretch();

  XCheckBox* checkbox3 = new XCheckBox();
  checkbox3->setText("Checkbox with really long text to test truncation");

  layer_2_layout->addWidget(checkbox3);

  return group;
}

QWidget* DebugTab::CreateRadioButtonGroup() {
  QWidget* group = new QWidget();

  QVBoxLayout* group_layout = new QVBoxLayout();
  group_layout->setContentsMargins(32, 0, 32, 0);
  group_layout->setSpacing(0);
  group->setLayout(group_layout);

  XGroupBox* groupbox = new XGroupBox("Radio Buttons");

  QVBoxLayout* groupbox_layout = new QVBoxLayout();
  groupbox_layout->setContentsMargins(16, 16, 16, 16);
  groupbox->setLayout(groupbox_layout);

  group_layout->addWidget(groupbox);

  QHBoxLayout* layer_1_layout = new QHBoxLayout();
  layer_1_layout->setContentsMargins(0, 0, 0, 0);
  layer_1_layout->setSpacing(20);

  QHBoxLayout* layer_2_layout = new QHBoxLayout();
  layer_2_layout->setContentsMargins(0, 0, 0, 0);
  layer_2_layout->setSpacing(20);

  groupbox_layout->addLayout(layer_1_layout);
  groupbox_layout->addLayout(layer_2_layout);

  group_layout->addLayout(groupbox_layout);

  XRadioButton* radio1 = new XRadioButton();
  radio1->setText("Test Radio Button 1");

  XRadioButton* radio2 = new XRadioButton();
  radio2->setText("Test Radio Button 2");

  layer_1_layout->addWidget(radio1);
  layer_1_layout->addWidget(radio2);

  layer_1_layout->addStretch();

  XRadioButton* radio3 = new XRadioButton();
  radio3->setText("Radio Button with really long text to test truncation");
  radio3->set_custom(true);
  radio3->set_checked_color(QColor(255, 150, 100));

  XRadioButton* radio4 = new XRadioButton();
  radio4->setText("Error");
  radio4->set_custom(true);
  radio4->set_checked_color(QColor(255, 0, 0));

  layer_2_layout->addWidget(radio3);
  layer_2_layout->addWidget(radio4);

  layer_2_layout->addStretch();

  // add radio buttons to their respective groups

  QButtonGroup* bg1 = new QButtonGroup();
  QButtonGroup* bg2 = new QButtonGroup();

  bg1->addButton(radio1);
  bg1->addButton(radio2);

  bg2->addButton(radio3);
  bg2->addButton(radio4);

  return group;
}

QWidget* DebugTab::CreateInputGroup() {
  QWidget* group = new QWidget();

  QVBoxLayout* group_layout = new QVBoxLayout();
  group_layout->setContentsMargins(32, 0, 32, 0);
  group_layout->setSpacing(0);
  group->setLayout(group_layout);

  XGroupBox* groupbox = new XGroupBox("Input Boxes");

  QVBoxLayout* groupbox_layout = new QVBoxLayout();
  groupbox_layout->setContentsMargins(16, 16, 16, 16);
  groupbox->setLayout(groupbox_layout);

  QLabel* lineedit_label = new QLabel("Line Edit");
  groupbox_layout->addWidget(lineedit_label);

  QHBoxLayout* line_layout = new QHBoxLayout();

  XLineEdit* line_edit = new XLineEdit("Text...");
  XLineEdit* disabled_line = new XLineEdit("Disabled");
  disabled_line->setDisabled(true);

  line_layout->addWidget(line_edit);
  line_layout->addWidget(disabled_line);

  groupbox_layout->addLayout(line_layout);

  QLabel* textedit_label = new QLabel("Text Edit");
  groupbox_layout->addWidget(textedit_label);

  QHBoxLayout* text_layout = new QHBoxLayout();

  XTextEdit* text_edit = new XTextEdit("Text...");
  text_edit->setMaximumHeight(80);

  XTextEdit* disabled_edit = new XTextEdit("Disabled");
  disabled_edit->setDisabled(true);
  disabled_edit->setMaximumHeight(80);

  text_layout->addWidget(text_edit);
  text_layout->addWidget(disabled_edit);

  groupbox_layout->addLayout(text_layout);

  group_layout->addWidget(groupbox);

  return group;
}

QWidget* DebugTab::CreateTab1Widget(XTabSelector* tab_selector,
                                    QStackedLayout* tab_stack_layout) {
  QWidget* widget = new QWidget();
  QVBoxLayout* layout = new QVBoxLayout(widget);
  layout->setSpacing(0);
  layout->setContentsMargins(16, 16, 16, 16);

  XPushButton* changeTabButton = new XPushButton("Go to Tab 2");
  changeTabButton->setMinimumSize(100, 24);
  connect(changeTabButton, &QPushButton::clicked, [=]() {
    tab_selector->SetTabIndex(1);
    tab_stack_layout->setCurrentIndex(1);
  });

  layout->addWidget(changeTabButton, 0, Qt::AlignCenter);
  layout->addStretch();

  return widget;
}

QWidget* DebugTab::CreateTab2Widget(XTabSelector* tab_selector,
                                    QStackedLayout* tab_stack_layout) {
  return nullptr;
}

QWidget* DebugTab::CreateTab3Widget(XTabSelector* tab_selector,
                                    QStackedLayout* tab_stack_layout) {
  return nullptr;
}

}  // namespace qt
}  // namespace ui
}  // namespace xe
