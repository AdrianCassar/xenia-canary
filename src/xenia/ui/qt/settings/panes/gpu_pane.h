#ifndef XENIA_UI_QT_GPU_PANE_H_
#define XENIA_UI_QT_GPU_PANE_H_

#include "settings_pane.h"

namespace xe {
namespace ui {
namespace qt {

class GPUPane : public SettingsPane {
  Q_OBJECT
 public:
  explicit GPUPane() : SettingsPane(QChar(0xE7F4), "GPU") {}

  void Build() override;
};

}  // namespace qt
}  // namespace ui
}  // namespace xe

#endif