#ifndef XENIA_UI_QT_INTERFACE_PANE_H_
#define XENIA_UI_QT_INTERFACE_PANE_H_

#include "settings_pane.h"

namespace xe {
namespace ui {
namespace qt {

class InterfacePane : public SettingsPane {
  Q_OBJECT
 public:
  explicit InterfacePane(QWidget* parent = nullptr)
      : SettingsPane(QChar(0xE790), "Interface", parent) {}

  void Build() override;
};

}  // namespace qt
}  // namespace ui
}  // namespace xe

#endif