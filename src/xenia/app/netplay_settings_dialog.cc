/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/netplay_settings_dialog.h"
#include "xenia/app/discord/discord_presence.h"
#include "xenia/app/emulator_window.h"
#include "xenia/base/logging.h"
#include "xenia/base/system.h"

DECLARE_bool(upnp);

DECLARE_bool(xhttp);

DECLARE_bool(logging);

DECLARE_bool(auto_check_updates);

DECLARE_string(api_address);

DECLARE_bool(discord);

DECLARE_int32(discord_presence_user_index);

namespace xe {
namespace app {

void NetplaySettingsDialog::OnDraw(ImGuiIO& io) {
  if (!dialog_opened_) {
    dialog_opened_ = true;
    ImGui::OpenPopup("Netplay Settings");
  }

  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImVec2 center = viewport->GetCenter();

  float btn_height_padding = ImGui::GetStyle().FramePadding.x * 4.0f;

  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSizeConstraints(ImVec2(350, -1), ImVec2(350, -1));
  if (ImGui::BeginPopupModal(
          "Netplay Settings", &dialog_opened_,
          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
    ImGui::SetWindowFontScale(1.05f);

    const auto emulator = emulator_window_->emulator();
    const auto kernel_state = emulator->kernel_state();
    const auto xlive_api = kernel_state->GetXboxLiveAPI();

    const float window_width = ImGui::GetContentRegionAvail().x;

    float btn_height = 25;
    float btn_width =
        (window_width * 0.5f) - (ImGui::GetStyle().ItemSpacing.x * 0.5f);
    ImVec2 half_width_btn = ImVec2(btn_width, btn_height);

    const bool updatable =
        xlive_api->GetInitState() == xe::kernel::XLiveAPI::InitState::Pending;

    ImGui::Text("API Addresses");

    ImGui::SetNextItemWidth(window_width);

    ImGui::BeginDisabled(!updatable);
    if (ImGui::BeginCombo("##API Addresses", selected_api_address_item_)) {
      for (const auto& api_address : api_addresses_) {
        const bool is_selected = selected_api_address_item_ == api_address;

        if (ImGui::Selectable(api_address.c_str(), is_selected)) {
          selected_api_address_item_ = api_address.c_str();
          xlive_api->SetAPIAddress(selected_api_address_item_);
        }

        if (is_selected) {
          ImGui::SetItemDefaultFocus();
        }
      }
      ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) &&
        !updatable) {
      ImGui::SetTooltip("Changing API address while playing isn't supported!");
    }

    const std::string remove_api_desc = "Remove";

    ImVec2 remove_api_lbl_size = ImGui::CalcTextSize(remove_api_desc.c_str());
    ImVec2 remove_api_btn_size =
        ImVec2(btn_width, remove_api_lbl_size.y + btn_height_padding);

    const bool is_default =
        selected_api_address_item_ == xlive_api->GetDefaultPublicServer();

    ImGui::BeginDisabled(!updatable || is_default);
    if (ImGui::Button(remove_api_desc.c_str(), remove_api_btn_size)) {
      ImGui::OpenPopup("Remove API Address");
    }
    ImGui::EndDisabled();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) &&
        !updatable) {
      ImGui::SetTooltip("Cannot remove API address while playing!");
    }

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) &&
        updatable && is_default) {
      ImGui::SetTooltip("Cannot remove default API address!");
    }

    ImGui::SameLine();

    const std::string add_api_desc = "Add";

    ImVec2 add_api_lbl_size = ImGui::CalcTextSize(add_api_desc.c_str());
    ImVec2 add_api_btn_size =
        ImVec2(btn_width, add_api_lbl_size.y + btn_height_padding);

    if (ImGui::Button(add_api_desc.c_str(), add_api_btn_size)) {
      add_api_address_dialog_open_ = true;
      ImGui::OpenPopup("API Addresses");
    }

    ImGui::Spacing();
    ImGui::Separator();

    ImGui::Text("Network Modes");

    ImGui::SetNextItemWidth(window_width);

    if (ImGui::Combo("##Network Modes", &selected_network_mode_index_,
                     network_modes_, std::size(network_modes_))) {
      switch (selected_network_mode_index_) {
        case xe::kernel::NETWORK_MODE::OFFLINE: {
          kernel_state->BroadcastNotification(
              kXNotificationLiveConnectionChanged,
              X_ONLINE_S_LOGON_DISCONNECTED);

          kernel_state->BroadcastNotification(
              kXNotificationLiveLinkStateChanged, 0);
        } break;
        case xe::kernel::NETWORK_MODE::LAN: {
          kernel_state->BroadcastNotification(
              kXNotificationLiveConnectionChanged,
              X_ONLINE_S_LOGON_DISCONNECTED);

          kernel_state->BroadcastNotification(
              kXNotificationLiveLinkStateChanged, 1);
        } break;
        case xe::kernel::NETWORK_MODE::XBOXLIVE: {
          kernel_state->BroadcastNotification(
              kXNotificationLiveConnectionChanged,
              X_ONLINE_S_LOGON_CONNECTION_ESTABLISHED);

          kernel_state->BroadcastNotification(
              kXNotificationLiveLinkStateChanged, 1);
        } break;
      }

      xlive_api->SetNetworkMode(selected_network_mode_index_);
    }

    ImGui::Spacing();
    ImGui::Separator();

    ImGui::Text("Network Interfaces");

    ImGui::SetNextItemWidth(window_width);

    ImGui::BeginDisabled(!updatable);
    if (ImGui::BeginCombo("##Network Interfaces",
                          selected_network_interface_item_)) {
      for (uint32_t index = 0;
           const auto& interface_name : network_interfaces_) {
        const bool is_selected =
            selected_network_interface_item_ == interface_name;

        if (ImGui::Selectable(interface_name.c_str(), is_selected)) {
          selected_interface_index_ = index;
          selected_network_interface_item_ = interface_name.c_str();

          const auto guid_keys = std::views::keys(network_interface_guids_);
          std::vector<std::string> guids = {guid_keys.begin(), guid_keys.end()};

          network_adapter_manager_->SetSelectedAdapterGUID(
              guids.at(selected_interface_index_));
        }

        if (is_selected) {
          ImGui::SetItemDefaultFocus();
        }

        index++;
      }
      ImGui::EndCombo();
    }
    ImGui::EndDisabled();

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) &&
        !updatable) {
      ImGui::SetTooltip(
          "Changing network interface while playing isn't supported!");
    }

    const std::string refresh_desc = "Refresh";

    ImVec2 refresh_desc_lbl_size = ImGui::CalcTextSize(refresh_desc.c_str());
    ImVec2 refresh_desc_btn_size =
        ImVec2(btn_width, refresh_desc_lbl_size.y + btn_height_padding);

    if (ImGui::Button(refresh_desc.c_str(), refresh_desc_btn_size)) {
      RefreshInterfaces();
    }

    ImGui::SameLine();

    const std::string reset_desc = "Reset";

    ImVec2 reset_lbl_size = ImGui::CalcTextSize(reset_desc.c_str());
    ImVec2 reset_btn_size =
        ImVec2(btn_width, reset_lbl_size.y + btn_height_padding);

    if (ImGui::Button(reset_desc.c_str(), reset_btn_size)) {
      network_adapter_manager_->SetSelectedAdapterGUID("");
      RefreshInterfaces();
    }

    ImGui::Spacing();
    ImGui::Separator();

    if (ImGui::Checkbox("Auto Check for Updates", &auto_check_for_updates_)) {
      emulator_window_->SetAutoCheckForUpdates(auto_check_for_updates_);
    }

    ImGui::SameLine();

    if (ImGui::Checkbox("NET Logging", &logging_)) {
      xlive_api->SetLogging(logging_);
    }

    ImGui::SameLine();

    if (ImGui::Checkbox("XHTTP", &xhttp_)) {
      xlive_api->SetXHttp(xhttp_);
    }

    if (ImGui::Checkbox("Discord Presence", &discord_)) {
      ActivateDiscordState(discord_);
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(225, 90), ImVec2(225, 90));
    if (ImGui::BeginPopupModal("Remove API Address", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      if (ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_GamepadFaceRight, false)) {
        ImGui::CloseCurrentPopup();
      }

      float btn_width = (ImGui::GetContentRegionAvail().x * 0.5f) -
                        (ImGui::GetStyle().ItemSpacing.x * 0.5f);
      ImVec2 btn_size = ImVec2(btn_width, btn_height);

      const std::string desc = "Are you sure?";

      ImVec2 desc_size = ImGui::CalcTextSize(desc.c_str());

      ImGui::SetCursorPosX((ImGui::GetWindowWidth() - desc_size.x) * 0.5f);
      ImGui::Text(desc.c_str());

      ImGui::Separator();

      if (ImGui::Button("Yes", btn_size)) {
        const auto xlive_api =
            emulator_window_->emulator()->kernel_state()->GetXboxLiveAPI();

        xlive_api->RemoveAPIAddress(selected_api_address_item_);

        UpdateAPIAddress();
        UpdateSelectedAPIItem();

        ImGui::CloseCurrentPopup();
      }

      ImGui::SameLine();

      if (ImGui::Button("Cancel", btn_size)) {
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    }

    ImGui::SetNextWindowContentSize(ImVec2(200, 0));
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("API Addresses", &add_api_address_dialog_open_,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      if (!add_address_context_open_ &&
          ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_GamepadFaceRight, false)) {
        ImGui::CloseCurrentPopup();
      }

      ImVec2 btn_size = ImVec2(ImGui::GetContentRegionAvail().x, btn_height);

      if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
      }

      ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
      const bool enter_pressed = ImGui::InputTextWithHint(
          "##AddAddress", "https://example.com", new_api_address_,
          sizeof(new_api_address_), ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::PopItemWidth();

      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip("Right Click/Gamepad A");
      }

      if (ImGui::IsItemFocused() &&
          ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_GamepadFaceDown, false)) {
        ImGui::OpenPopup("##AddAddressContexts");
      }

      if (ImGui::BeginPopupContextItem("##AddAddressContexts")) {
        add_address_context_open_ = true;

        if (ImGui::MenuItem("Paste")) {
          const std::string clipboard = ImGui::GetClipboardText();

          if (!clipboard.empty()) {
            // Null terminated string
            const std::string safe_clipboard = xe::string_util::trim(
                clipboard.substr(0, sizeof(new_api_address_) - 1));

            strcpy(new_api_address_, safe_clipboard.c_str());
          }
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Clear")) {
          memset(new_api_address_, 0, sizeof(new_api_address_));
        }

        ImGui::EndPopup();
      } else {
        add_address_context_open_ = false;
      }

      if (ImGui::Button("Add", btn_size) || enter_pressed) {
        const std::string api_address(new_api_address_);

        if (!api_address.empty()) {
          xlive_api->AddAPIAddress(api_address);

          UpdateAPIAddress();
          UpdateSelectedAPIItem();

          ImGui::CloseCurrentPopup();
        }
      }

      ImGui::EndPopup();
    }

    ImGui::SetWindowFontScale(1.0f);

    ImGui::EndPopup();
  }

  if (!dialog_opened_) {
    Close();
    ImGui::CloseCurrentPopup();
    emulator_window_->ToggleNetplaySettingsDialog();
  }
}

void NetplaySettingsDialog::UpdateInterfaceGUIDs() {
  for (const auto& adapter : network_adapter_manager_->GetAdapters()) {
    const std::string guid = adapter.AdapterName;
    const std::string interface_name =
        network_adapter_manager_->GetAdapterFriendlyName(adapter);

    network_interface_guids_[guid] = interface_name;
  }

  const auto network_interfaces = std::views::values(network_interface_guids_);
  network_interfaces_ = {network_interfaces.begin(), network_interfaces.end()};
}

void NetplaySettingsDialog::UpdateSelectedInterfaceItemAndIndex() {
  const auto guid_keys = std::views::keys(network_interface_guids_);

  const auto it = std::ranges::find(
      guid_keys, network_adapter_manager_->GetSelectedAdapaterGUID());

  if (it != guid_keys.end()) {
    selected_interface_index_ = std::distance(guid_keys.begin(), it);
    selected_network_interface_item_ = network_interface_guids_.at(*it).c_str();
  } else {
    selected_interface_index_ = 0;

    if (guid_keys.empty()) {
      selected_network_interface_item_ = "Unspecified Network";
    }
  }
}

void NetplaySettingsDialog::RefreshInterfaces() {
  network_adapter_manager_->RefreshNetworkAdapters();

  network_interfaces_.clear();
  network_interface_guids_.clear();

  UpdateInterfaceGUIDs();
  UpdateSelectedInterfaceItemAndIndex();
}

void xe::app::NetplaySettingsDialog::UpdateAPIAddress() {
  api_addresses_ =
      emulator_window_->emulator()->GetXboxLiveAPI()->ParseAPIList();
}

void xe::app::NetplaySettingsDialog::UpdateSelectedAPIItem() {
  const std::string current_api_address =
      emulator_window_->emulator()->GetXboxLiveAPI()->GetApiAddress();

  const auto it = std::ranges::find(api_addresses_, current_api_address);

  if (it != api_addresses_.end()) {
    selected_api_address_item_ = it->c_str();
  } else {
    if (api_addresses_.empty()) {
      selected_api_address_item_ = api_addresses_.front().c_str();
    } else {
      selected_api_address_item_ = "Unspecified API Address";
    }
  }
}

void xe::app::NetplaySettingsDialog::UpdateSelectedNetworkModeIndex() {
  selected_network_mode_index_ = cvars::network_mode;
}

void xe::app::NetplaySettingsDialog::InitializeCheckboxSettings() {
  xhttp_ = cvars::xhttp;
  auto_check_for_updates_ = cvars::auto_check_updates;
  logging_ = cvars::logging;
  discord_ = cvars::discord;
}

void NetplaySettingsDialog::ActivateDiscordState(bool state) {
  const auto emulator = emulator_window_->emulator();
  const auto kernel_state = emulator->kernel_state();

  discord::DiscordPresence::SetDiscordState(state);

  if (cvars::discord) {
    discord::DiscordPresence::Initialize();

    if (emulator->is_title_open()) {
      const auto user = kernel_state->xam_state()->GetUserProfile(
          uint32_t(cvars::discord_presence_user_index));

      if (user) {
        discord::DiscordPresence::PlayingTitle(
            emulator->title_name(), xe::to_utf8(user->GetPresenceString()));
      }
    } else {
      discord::DiscordPresence::NotPlaying();
    }

    discord::DiscordPresence::Update();
  } else {
    discord::DiscordPresence::Shutdown();
  }
}

void NetplayStatusDialog::OnDraw(ImGuiIO& io) {
  if (!dialog_opened_) {
    dialog_opened_ = true;
    ImGui::OpenPopup("Netplay Status");
  }

  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImVec2 center = viewport->GetCenter();

  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSizeConstraints(ImVec2(450, -1), ImVec2(450, -1));
  if (ImGui::BeginPopupModal(
          "Netplay Status", &dialog_opened_,
          ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
    if (ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_GamepadFaceRight, false)) {
      ImGui::CloseCurrentPopup();
    }

    ImGui::SetWindowFontScale(1.10f);

    const auto emulator = emulator_window_->emulator();
    const auto kernel_state = emulator->kernel_state();
    const auto xlive_api = kernel_state->GetXboxLiveAPI();

    std::string xlive_api_state;
    std::string upnp_state;
    std::string api_server_state;

    const bool is_pending =
        xlive_api->GetInitState() == xe::kernel::XLiveAPI::InitState::Pending;
    const bool is_success =
        xlive_api->GetInitState() == xe::kernel::XLiveAPI::InitState::Success;
    const bool is_failed =
        xlive_api->GetInitState() == xe::kernel::XLiveAPI::InitState::Failed;

    if (is_pending) {
      xlive_api_state = "XLiveAPI Initialized: Pending Initialization";
      upnp_state = "UPnP: Pending Initialization";
      api_server_state = "API Server: Pending Initialization";
    } else {
      xlive_api_state = fmt::format("XLiveAPI Initialized: {}",
                                    is_success ? "True" : "False");

      if (cvars::upnp) {
        const auto upnp = emulator->GetUPnP();

        if (upnp && upnp->IsActive()) {
          upnp_state = "UPnP: Device found";
        } else {
          upnp_state = "UPnP: Device search failed";
        }
      }

      api_server_state = fmt::format("Communication {} with API address:",
                                     is_success ? "succeeded" : "failed");
    }

    ImGui::Text(xlive_api_state.c_str());
    ImGui::Spacing();

    ImGui::Text(upnp_state.c_str());
    ImGui::Spacing();

    if (is_failed) {
      ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 50, 50, 255));
    }

    ImGui::Text(api_server_state.c_str());

    if (!is_pending) {
      // We need to use a separate thread otherwise window will freeze.
      if (ImGui::TextLink(cvars::api_address.c_str())) {
        std::jthread open_link(LaunchWebBrowser, cvars::api_address);
        open_link.detach();
      }
    }

    ImGui::Spacing();

    if (is_failed) {
      ImGui::PopStyleColor();
    }

    if (xlive_api->IsXUIDMismatched()) {
      ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 50, 50, 255));
      ImGui::Text("XUID mismatch expect unstable netplay!");
      ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(50, 240, 50, 255));
      ImGui::Text(
          "Go to Netplay->Manager->Delete Netplay Profiles to fix this issue.");
      ImGui::PopStyleColor(2);
    }

    ImGui::SetWindowFontScale(1.0f);

    ImGui::EndPopup();
  }

  if (!dialog_opened_) {
    Close();
    ImGui::CloseCurrentPopup();
    emulator_window_->ToggleNetplayStatusDialog();
  }
}

}  // namespace app
}  // namespace xe
