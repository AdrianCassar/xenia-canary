#ifndef XENIA_UI_QT_ADVANCED_PANE_H_
#define XENIA_UI_QT_ADVANCED_PANE_H_

#include "settings_pane.h"

namespace xe {
namespace ui {
namespace qt {

class AdvancedPane : public SettingsPane {
  Q_OBJECT
 public:
  explicit AdvancedPane(QWidget* parent = nullptr)
      : SettingsPane(QChar(0xE7BA), "Advanced", parent) {}

  void Build() override;
};

}  // namespace qt
}  // namespace ui
}  // namespace xe

#endif