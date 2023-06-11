#ifndef XENIA_UI_QT_ADVANCED_PANE_H_
#define XENIA_UI_QT_ADVANCED_PANE_H_

#include "settings_pane.h"

namespace xe {
namespace ui {
namespace qt {

class AdvancedPane : public SettingsPane {
  Q_OBJECT
 public:
  explicit AdvancedPane() : SettingsPane(QChar(0xE7BA), "Advanced") {}

  void Build() override;
};

}  // namespace qt
}  // namespace ui
}  // namespace xe

#endif