#ifndef XENIA_UI_QT_CPU_PANE_H_
#define XENIA_UI_QT_CPU_PANE_H_

#include "settings_pane.h"

namespace xe {
namespace ui {
namespace qt {

class CPUPane : public SettingsPane {
  Q_OBJECT
 public:
  explicit CPUPane() : SettingsPane(QChar(0xEC4A), "CPU") {}

  void Build() override;
};

}  // namespace qt
}  // namespace ui
}  // namespace xe

#endif