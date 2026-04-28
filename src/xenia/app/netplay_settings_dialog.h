/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_NETPLAY_SETTINGS_DIALOG_H_
#define XENIA_APP_NETPLAY_SETTINGS_DIALOG_H_

#include <map>
#include <ranges>

#include "xenia/kernel/util/network_adapter_manager.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"

namespace xe {
namespace app {

class EmulatorWindow;  // Forward declaration due to circular dependency

class NetplaySettingsDialog final : public ui::ImGuiDialog {
 public:
  NetplaySettingsDialog(ui::ImGuiDrawer* imgui_drawer,
                        EmulatorWindow* emulator_window,
                        kernel::NetworkAdapterManager* network_dapter_manager)
      : ui::ImGuiDialog(imgui_drawer),
        emulator_window_(emulator_window),
        network_adapter_manager_(network_dapter_manager) {
    UpdateInterfaceGUIDs();
    UpdateSelectedInterfaceItemAndIndex();

    UpdateAPIAddress();
    UpdateSelectedAPIItem();

    UpdateSelectedNetworkModeIndex();

    InitializeCheckboxSettings();
  }

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  void UpdateInterfaceGUIDs();
  void UpdateSelectedInterfaceItemAndIndex();
  void RefreshInterfaces();

  void UpdateAPIAddress();
  void UpdateSelectedAPIItem();

  void UpdateSelectedNetworkModeIndex();

  void InitializeCheckboxSettings();

  bool dialog_opened_ = false;

  int selected_interface_index_ = 0;
  const char* selected_network_interface_item_ = nullptr;
  std::vector<std::string> network_interfaces_;
  std::unordered_map<std::string, std::string> network_interface_guids_;

  bool add_api_address_dialog_open_ = false;
  bool add_address_context_open_ = false;
  const char* selected_api_address_item_ = nullptr;
  std::vector<std::string> api_addresses_;
  char new_api_address_[100] = {};

  int selected_network_mode_index_ = 0;
  const char* network_modes_[3] = {"Offline", "Systemlink", "Xbox Live"};

  bool xhttp_ = false;
  bool logging_ = false;
  bool auto_check_for_updates_ = false;

  EmulatorWindow* emulator_window_ = nullptr;
  kernel::NetworkAdapterManager* network_adapter_manager_ = nullptr;
};

class NetplayStatusDialog final : public ui::ImGuiDialog {
 public:
  NetplayStatusDialog(ui::ImGuiDrawer* imgui_drawer,
                      EmulatorWindow* emulator_window,
                      kernel::NetworkAdapterManager* network_dapter_manager)
      : ui::ImGuiDialog(imgui_drawer),
        emulator_window_(emulator_window),
        network_adapter_manager_(network_dapter_manager) {}

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  bool dialog_opened_ = false;
  EmulatorWindow* emulator_window_ = nullptr;
  kernel::NetworkAdapterManager* network_adapter_manager_ = nullptr;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_NETPLAY_SETTINGS_DIALOG_H_
