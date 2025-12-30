/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <memory>

#include "xenia/kernel/xam/ui/create_profile_ui.h"
#include "xenia/kernel/xam/ui/signin_ui.h"

#include "xenia/kernel/XLiveAPI.h"

namespace xe {
namespace kernel {
namespace xam {
namespace ui {

SigninUI::SigninUI(xe::ui::ImGuiDrawer* imgui_drawer,
                   ProfileManager* profile_manager, uint32_t last_used_slot,
                   uint32_t users_needed, uint32_t flags)
    : XamDialog(imgui_drawer),
      profile_manager_(profile_manager),
      last_user_(last_used_slot),
      users_needed_(users_needed),
      flags_(flags),
      title_("Sign In") {
  if (flags_ & X_UI_FLAGS_ONLINEENABLED) {
    title_ = "Sign In - Xbox Live Enabled Profiles";
  }

  const auto gamerpic_key = CreateProfileUI::GetDefaultGamerPictureKey();

  if (gamerpic_key.has_value()) {
    create_profile_args_.gamerpic_key = gamerpic_key;
    create_profile_args_.downloaded_gamerpics =
        XLiveAPI::DownloadCompleteGamerpic(gamerpic_key.value());
  }
}

void SigninUI::OnDraw(ImGuiIO& io) {
  if (!has_opened_) {
    ImGui::OpenPopup(title_.c_str());
    has_opened_ = true;
    ReloadProfiles(true, flags_);
  }
  if (ImGui::BeginPopupModal(title_.c_str(), nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    for (uint32_t i = 0; i < users_needed_; i++) {
      ImGui::BeginGroup();

      std::vector<const char*> combo_items;
      int items_count = 0;
      int current_item = 0;

      // Fill slot list.
      std::vector<uint8_t> slots;
      slots.push_back(XUserIndexAny);
      combo_items.push_back("---");
      for (auto& elem : slot_data_) {
        // Select the slot or skip it if it's already used.
        bool already_taken = false;
        for (uint32_t j = 0; j < users_needed_; j++) {
          if (chosen_slots_[j] == elem.first) {
            if (i == j) {
              current_item = static_cast<int>(combo_items.size());
            } else {
              already_taken = true;
            }
            break;
          }
        }

        if (already_taken) {
          continue;
        }

        slots.push_back(elem.first);
        combo_items.push_back(elem.second.c_str());
      }
      items_count = static_cast<int>(combo_items.size());

      ImGui::BeginDisabled(users_needed_ == 1);
      ImGui::Combo(fmt::format("##Slot{:d}", i).c_str(), &current_item,
                   combo_items.data(), items_count);
      chosen_slots_[i] = slots[current_item];
      ImGui::EndDisabled();
      ImGui::Spacing();

      combo_items.clear();
      current_item = 0;

      // Fill profile list.
      std::vector<uint64_t> xuids;
      xuids.push_back(0);
      combo_items.push_back("---");
      if (chosen_slots_[i] != XUserIndexAny) {
        for (auto& elem : profile_data_) {
          // Select the profile or skip it if it's already used.
          bool already_taken = false;
          for (uint32_t j = 0; j < users_needed_; j++) {
            if (chosen_xuids_[j] == elem.first) {
              if (i == j) {
                current_item = static_cast<int>(combo_items.size());
              } else {
                already_taken = true;
              }
              break;
            }
          }

          if (already_taken) {
            continue;
          }

          xuids.push_back(elem.first);
          combo_items.push_back(elem.second.c_str());
        }
      }
      items_count = static_cast<int>(combo_items.size());

      ImGui::BeginDisabled(chosen_slots_[i] == XUserIndexAny);
      ImGui::Combo(fmt::format("##Profile{:d}", i).c_str(), &current_item,
                   combo_items.data(), items_count);
      chosen_xuids_[i] = xuids[current_item];
      ImGui::EndDisabled();
      ImGui::Spacing();

      // Draw profile badge.
      uint8_t slot = chosen_slots_[i];
      uint64_t xuid = chosen_xuids_[i];
      const auto account = profile_manager_->GetAccount(xuid);

      if (slot != XUserIndexAny && account) {
        xeDrawProfileContent(imgui_drawer(), xuid, slot, account, nullptr, {},
                             {}, nullptr);
      }

      ImGui::EndGroup();
      if (i != (users_needed_ - 1) && (i == 0 || i == 2)) {
        ImGui::SameLine();
      }
    }

    ImGui::Spacing();

    if (ImGui::Button("Create Profile")) {
      create_profile_args_.dialog_open = true;
      ImGui::OpenPopup("Create Profile");
    }
    ImGui::Spacing();

    if (create_profile_args_.dialog_open) {
      if (!xeDrawCreateProfile(imgui_drawer(), kernel_state()->emulator(),
                               create_profile_args_)) {
        ReloadProfiles(false, flags_);
      }
    }

    if (ImGui::Button("OK")) {
      std::map<uint8_t, uint64_t> profile_map;
      for (uint32_t i = 0; i < users_needed_; i++) {
        uint8_t slot = chosen_slots_[i];
        uint64_t xuid = chosen_xuids_[i];
        if (slot != XUserIndexAny && xuid != 0) {
          profile_map[slot] = xuid;
        }
      }
      profile_manager_->LoginMultiple(profile_map);

      ImGui::CloseCurrentPopup();
      Close();
    }
    ImGui::SameLine();

    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
      Close();
    }

    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::EndPopup();
  } else {
    Close();
  }
}

void SigninUI::ReloadProfiles(bool first_draw, uint32_t flags) {
  auto profile_manager = kernel_state()->xam_state()->profile_manager();
  auto profiles = profile_manager->GetAccounts();

  profile_data_.clear();
  for (auto& [xuid, account] : *profiles) {
    if (flags_ & X_UI_FLAGS_ONLINEENABLED) {
      if (account.IsLiveEnabled()) {
        profile_data_.push_back({xuid, account.GetGamertagString()});
      }
    } else {
      profile_data_.push_back({xuid, account.GetGamertagString()});
    }
  }

  if (first_draw) {
    // If only one user is requested, request last used controller to sign in.
    if (users_needed_ == 1) {
      chosen_slots_[0] = last_user_;
    } else {
      for (uint32_t i = 0; i < users_needed_; i++) {
        // TODO: Not sure about this, needs testing on real hardware.
        chosen_slots_[i] = i;
      }
    }

    // Default profile selection to profile that is already signed in.
    for (auto& elem : profile_data_) {
      uint64_t xuid = elem.first;
      uint8_t slot = profile_manager->GetUserIndexAssignedToProfile(xuid);
      for (uint32_t j = 0; j < users_needed_; j++) {
        if (chosen_slots_[j] != XUserIndexAny && slot == chosen_slots_[j]) {
          chosen_xuids_[j] = xuid;
        }
      }
    }
  }
}

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe
