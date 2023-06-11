#ifndef XENIA_UI_QT_CONTROLS_PANE_H_
#define XENIA_UI_QT_CONTROLS_PANE_H_

#include "settings_pane.h"

namespace xe {
namespace ui {
namespace qt {

class ControlsPane : public SettingsPane {
  Q_OBJECT
 public:
  explicit ControlsPane() : SettingsPane(QChar(0xE7FC), "Controls") {}

  void Build() override;
};

}  // namespace qt
}  // namespace ui
}  // namespace xe

#endif