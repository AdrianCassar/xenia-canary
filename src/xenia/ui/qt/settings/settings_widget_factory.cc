/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "settings_widget_factory.h"
#include <QHBoxLayout>
#include "xenia/ui/qt/settings/widgets/settings_checkbox.h"
#include "xenia/ui/qt/settings/widgets/settings_combobox.h"
#include "xenia/ui/qt/settings/widgets/settings_line_edit.h"
#include "xenia/ui/qt/settings/widgets/settings_slider.h"
#include "xenia/ui/qt/widgets/groupbox.h"
#include "xenia/ui/qt/widgets/push_button.h"
#include "xenia/ui/qt/widgets/scroll_area.h"

namespace xe {
namespace ui {
namespace qt {

const double kSubLabelSize = 6.5;
const int kLineEditMaxWidth = 420;

QLabel* create_title_label(const std::string& title) {
  auto label = new QLabel(title.c_str());
  label->setObjectName("titleLabel");
  return label;
}

QWidget* SettingsWidgetFactory::BuildSettingsWidget(
    const std::string& set_name) {
  const auto& set = settings_.FindSettingsSet(set_name);

  QWidget* base_widget = new QWidget();

  QVBoxLayout* layout = new QVBoxLayout();
  base_widget->setLayout(layout);

  for (const auto& group : set->GetSettingsGroups()) {
    XGroupBox* group_box = new XGroupBox(group->title().c_str());
    QVBoxLayout* gb_layout = new QVBoxLayout();
    group_box->setLayout(gb_layout);

    for (auto& item : group->items()) {
      QWidget* settings_widget = CreateWidgetForSettingsItem(*item);
      if (settings_widget) {
        gb_layout->addWidget(settings_widget);
      }
    }
    layout->addWidget(group_box);
  }

  base_widget->setObjectName("settingsContainer");

  return base_widget;
}

QWidget* SettingsWidgetFactory::CreateWidgetForSettingsItem(
    ISettingsItem& item) {
  try {
    switch (item.settings_type()) {
      case SettingsType::Switch: {
        return CreateCheckBoxWidget(dynamic_cast<SwitchSettingsItem&>(item));
        break;
      }
      case SettingsType::TextInput: {
        return CreateTextInputWidget(
            dynamic_cast<TextInputSettingsItem&>(item));
        break;
      }
      case SettingsType::PathInput: {
        return CreatePathInputWidget(
            dynamic_cast<PathInputSettingsItem&>(item));
        break;
      }
      case SettingsType::NumberInput: {
        return CreateNumberInputWidget(
            dynamic_cast<NumberInputSettingsItem&>(item));
        break;
      }
      case SettingsType::Slider: {
        return CreateRangeInputWidget(dynamic_cast<SliderSettingsItem&>(item));
        break;
      }
      case SettingsType::MultiChoice: {
        return CreateMultiChoiceWidget(
            dynamic_cast<IMultiChoiceSettingsItem&>(item));
        break;
      }
      case SettingsType::Action: {
        return CreateActionWidget(dynamic_cast<ActionSettingsItem&>(item));
        break;
      }
      case SettingsType::Custom: {
        return nullptr;
        // TODO: when/if custom widgets are required, build them here
        break;
      }
    }
  } catch (const std::bad_cast&) {
    XELOGE("SettingsItem \"{}\" had wrong type value", item.title());
  }

  return nullptr;
}

QWidget* SettingsWidgetFactory::CreateCheckBoxWidget(SwitchSettingsItem& item) {
  SettingsCheckBox* checkbox = new SettingsCheckBox(item);

  return CreateWidgetContainer(checkbox);
}

QWidget* SettingsWidgetFactory::CreateTextInputWidget(
    TextInputSettingsItem& item) {
  QWidget* ctr = new QWidget();
  QVBoxLayout* ctr_layout = new QVBoxLayout();
  ctr->setLayout(ctr_layout);

  QLabel* title_label = create_title_label(item.title());

  SettingsLineEdit* line_edit = new SettingsLineEdit(item);

  ctr_layout->addWidget(title_label);
  ctr_layout->addWidget(line_edit);

  return CreateWidgetContainer(ctr);
}

QWidget* SettingsWidgetFactory::CreatePathInputWidget(
    PathInputSettingsItem& item) {
  QWidget* ctr = new QWidget();
  QVBoxLayout* ctr_layout = new QVBoxLayout();
  ctr->setLayout(ctr_layout);
  ctr_layout->setContentsMargins(0, 0, 0, 0);
  ctr_layout->setSpacing(8);

  QLabel* title_label = create_title_label(item.title());

  QHBoxLayout* control_layout = new QHBoxLayout();
  control_layout->setSpacing(8);

  SettingsLineEdit* line_edit = new SettingsLineEdit(item);
  XPushButton* browse_btn = new XPushButton();
  browse_btn->SetIconFromGlyph(QChar(0xE838));
  // TODO: Setup browse button logic

  control_layout->addWidget(line_edit);
  control_layout->addWidget(browse_btn);
  control_layout->addStretch();

  ctr_layout->addWidget(title_label);
  ctr_layout->addLayout(control_layout);

  return CreateWidgetContainer(ctr);
}

QWidget* SettingsWidgetFactory::CreateNumberInputWidget(
    NumberInputSettingsItem& item) {
  // TODO: use XSpinBox (styled QSpinBox)
  return nullptr;
}

QWidget* SettingsWidgetFactory::CreateRangeInputWidget(
    SliderSettingsItem& item) {
  QWidget* ctr = new QWidget();
  QHBoxLayout* ctr_layout = new QHBoxLayout();
  ctr_layout->setContentsMargins(0, 0, 0, 0);
  ctr_layout->setSpacing(20);
  ctr->setLayout(ctr_layout);

  QLabel* title_label = create_title_label(item.title());

  SettingsSlider* slider = new SettingsSlider(item);

  /*SettingsSlider::connect(slider, &SettingsSlider::valueChanged,
                          [&](int value) { item.UpdateValue(value); });*/

  ctr_layout->addWidget(title_label);
  ctr_layout->addWidget(slider);
  ctr_layout->addStretch();

  return CreateWidgetContainer(ctr);
}

QWidget* SettingsWidgetFactory::CreateMultiChoiceWidget(
    IMultiChoiceSettingsItem& item) {
  QWidget* ctr = new QWidget();
  QHBoxLayout* ctr_layout = new QHBoxLayout();
  ctr_layout->setContentsMargins(0, 0, 0, 0);
  ctr_layout->setSpacing(20);
  ctr->setLayout(ctr_layout);

  QLabel* title_label = create_title_label(item.title());

  SettingsComboBox* combobox = new SettingsComboBox(item);

  ctr_layout->addWidget(title_label);
  ctr_layout->addWidget(combobox);
  ctr_layout->addStretch();

  return CreateWidgetContainer(ctr);
}

QWidget* SettingsWidgetFactory::CreateActionWidget(ActionSettingsItem& item) {
  return nullptr;
}

QWidget* SettingsWidgetFactory::CreateWidgetContainer(QWidget* target_widget) {
  QWidget* container_widget = new QWidget();
  QVBoxLayout* widget_layout = new QVBoxLayout();
  widget_layout->setContentsMargins(0, 0, 0, 0);
  widget_layout->setSpacing(0);
  container_widget->setLayout(widget_layout);

  // label used to show warnings
  QLabel* widget_label = new QLabel();
  widget_label->setObjectName("subLabel");
  widget_label->setProperty("type", "warning");
  auto font = widget_label->font();
  font.setPointSizeF(kSubLabelSize);
  widget_label->setFont(font);
  widget_label->setVisible(false);

  widget_layout->addWidget(target_widget);
  widget_layout->addWidget(widget_label);

  return container_widget;
}

}  // namespace qt
}  // namespace ui
}  // namespace xe