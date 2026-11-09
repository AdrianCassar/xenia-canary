/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <algorithm>
#include <cfloat>

#include "xenia/app/emulator_window.h"
#include "xenia/app/profile_dialogs.h"
#include "xenia/base/png_utils.h"
#include "xenia/base/system.h"
#include "xenia/kernel/XLiveAPI.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_ui.h"
#include "xenia/ui/file_picker.h"
#include "xenia/ui/imgui_host_notification.h"

#include <xenia/vfs/devices/host_path_entry.h>
#include "xenia/kernel/xam/ui/create_profile_ui.h"
#include "xenia/kernel/xam/ui/friends_ui_util.h"
#include "xenia/kernel/xam/ui/gamercard_ui.h"
#include "xenia/kernel/xam/ui/signin_ui.h"
#include "xenia/kernel/xam/ui/title_info_ui.h"

namespace xe {
namespace app {

void NoProfileDialog::OnDraw(ImGuiIO& io) {
  auto profile_manager = emulator_window_->emulator()
                             ->kernel_state()
                             ->xam_state()
                             ->profile_manager();

  if (profile_manager->GetAccountCount()) {
    Close();
    return;
  }

  const auto window_position =
      ImVec2(GetIO().DisplaySize.x * 0.35f, GetIO().DisplaySize.y * 0.4f);

  ImGui::SetNextWindowPos(window_position, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(1.0f);

  bool dialog_open = true;
  if (!ImGui::Begin("No Profiles Found", &dialog_open,
                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                        ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    ImGui::End();
    Close();
    return;
  }

  const std::string message =
      "There is no profile available! You will not be able to save without "
      "one.\n\nWould you like to create one?";

  ImGui::TextUnformatted(message.c_str());

  ImGui::Separator();
  ImGui::NewLine();

  const auto content_files = xe::filesystem::ListDirectories(
      emulator_window_->emulator()->content_root());

  if (ImGui::IsWindowAppearing()) {
    ImGui::SetKeyboardFocusHere();
  }
  if (content_files.empty()) {
    if (ImGui::Button("Create Profile")) {
      new kernel::xam::ui::CreateProfileUI(emulator_window_->imgui_drawer(),
                                           emulator_window_->emulator());
    }
  } else {
    if (ImGui::Button("Create profile & migrate data")) {
      new kernel::xam::ui::CreateProfileUI(emulator_window_->imgui_drawer(),
                                           emulator_window_->emulator(), true);
    }
  }

  ImGui::SameLine();
  if (ImGui::Button("Open profile menu")) {
    emulator_window_->ToggleProfilesConfigDialog();
  }

  ImGui::SameLine();
  if (ImGui::Button("Close") || !dialog_open) {
    emulator_window_->SetHotkeysState(true);
    ImGui::End();
    Close();
    return;
  }
  ImGui::End();
}

void ProfileConfigDialog::LoadProfileIcon() {
  if (!emulator_window_) {
    return;
  }

  const auto profile_manager = emulator_window_->emulator()
                                   ->kernel_state()
                                   ->xam_state()
                                   ->profile_manager();
  if (!profile_manager) {
    return;
  }

  for (const auto& [xuid, account] : *profile_manager->GetAccounts()) {
    LoadProfileIcon(xuid);
  }
}

void ProfileConfigDialog::LoadProfileIcon(const uint64_t xuid) {
  if (!emulator_window_) {
    return;
  }

  const auto xam_state =
      emulator_window_->emulator()->kernel_state()->xam_state();

  const auto profile_manager = xam_state->profile_manager();
  const auto user_tracker = xam_state->user_tracker();

  if (!profile_manager) {
    return;
  }

  if (!user_tracker) {
    return;
  }

  const auto gamerpic_key = user_tracker->GetUserGamerpicSetting(xuid);

  if (gamerpic_key.has_value()) {
    profile_gamerpic_key_[xuid] = gamerpic_key.value();
  } else {
    profile_gamerpic_key_.erase(xuid);
  }

  const auto profile_icon =
      profile_manager->GetAccountIcon(xuid, kernel::xam::XTileType::kGamerTile);

  if (auto& current_icon = profile_icon_[xuid]; current_icon) {
    released_icons_.push_back(std::move(current_icon));
  }

  profile_icon_[xuid] = profile_icon.empty()
                            ? nullptr
                            : imgui_drawer()->LoadImGuiIcon(profile_icon);
}

void ProfileConfigDialog::LoadSignedInProfilesCount() {
  signed_in_profiles_count_ = emulator_window_->emulator()
                                  ->kernel_state()
                                  ->xam_state()
                                  ->profile_manager()
                                  ->SignedInProfilesCount();
}

void ProfileConfigDialog::OnDraw(ImGuiIO& io) {
  if (!emulator_window_->emulator() ||
      !emulator_window_->emulator()->kernel_state() ||
      !emulator_window_->emulator()->kernel_state()->xam_state()) {
    return;
  }

  auto profile_manager = emulator_window_->emulator()
                             ->kernel_state()
                             ->xam_state()
                             ->profile_manager();
  if (!profile_manager) {
    return;
  }

  // Previous frame is already rendered, so these aren't referenced anymore.
  released_icons_.clear();

  for (const uint64_t xuid : *pending_icon_reloads_) {
    LoadProfileIcon(xuid);
  }
  pending_icon_reloads_->clear();

  auto profiles = profile_manager->GetAccounts();

  ImGui::SetNextWindowPos(ImVec2(40, 40), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.8f);

  bool dialog_open = true;
  if (!ImGui::Begin("Profiles Menu", &dialog_open,
                    ImGuiWindowFlags_NoCollapse |
                        ImGuiWindowFlags_AlwaysAutoResize |
                        ImGuiWindowFlags_HorizontalScrollbar)) {
    ImGui::End();
    return;
  }

  // For whatever reason dialog wasn't opened. It's probably in closing state.
  // We need to handle it here before it will make icons allocation.
  if (!dialog_open) {
    ImGui::CloseCurrentPopup();
    Close();
    ImGui::End();
    emulator_window_->ToggleProfilesConfigDialog();
    return;
  }

  if (profiles->empty()) {
    ImGui::TextUnformatted("No profiles found!");
    ImGui::Spacing();
    ImGui::Separator();
  }

  const ImVec2 next_window_position =
      ImVec2(ImGui::GetWindowPos().x + ImGui::GetWindowSize().x + 20.f,
             ImGui::GetWindowPos().y);

  // Load profile icon for profiles created while dialog is rendering.
  if (signed_in_profiles_count_ != profile_manager->SignedInProfilesCount()) {
    LoadProfileIcon();
    signed_in_profiles_count_ = profile_manager->SignedInProfilesCount();
  }

  // Scroll instead of running off the window, keeping Create Profile visible.
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  const ImGuiStyle& style = ImGui::GetStyle();
  const float reserved_height = style.ItemSpacing.y * 2.0f +
                                ImGui::GetFrameHeightWithSpacing() +
                                style.WindowPadding.y;
  const float max_list_height =
      std::max(viewport->WorkPos.y + viewport->WorkSize.y -
                   ImGui::GetCursorScreenPos().y - reserved_height,
               ui::default_image_icon_size.y * 1.5f);
  ImGui::SetNextWindowSizeConstraints(ImVec2(0.0f, 0.0f),
                                      ImVec2(FLT_MAX, max_list_height));
  ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, ImGui::GetFontSize() * 0.8f);
  ImGui::BeginChild("##ProfileList", ImVec2(0.0f, 0.0f),
                    ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AutoResizeY);
  ImGui::PopStyleVar();

  for (auto& [xuid, account] : *profiles) {
    ImGui::PushID(fmt::format("{:016X}", xuid).c_str());

    const uint8_t user_index =
        profile_manager->GetUserIndexAssignedToProfile(xuid);

    // Profile might be created after dialog was opened.
    if (!profile_icon_.contains(xuid)) {
      LoadProfileIcon(xuid);
    }

    const auto profile_icon = profile_icon_[xuid].get();

    auto context_menu_fun = [=, this]() -> bool {
      if (ImGui::BeginPopupContextItem("Profile Menu")) {
        //*selected_xuid = xuid;
        if (user_index == XUserIndexAny) {
          ImGui::BeginDisabled(!profile_manager->IsAnyProfileSlotFree());

          if (ImGui::MenuItem("Login")) {
            profile_manager->Login(xuid);
            LoadProfileIcon(xuid);
          }
          if (ImGui::BeginMenu("Login to slot:")) {
            for (uint8_t i = 1; i <= XUserMaxUserCount; i++) {
              if (ImGui::MenuItem(fmt::format("slot {}", i).c_str())) {
                uint64_t current_slot_xuid = 0;

                if (const auto current_profile = profile_manager->GetProfile(
                        static_cast<uint8_t>(i - 1));
                    current_profile) {
                  current_slot_xuid = current_profile->xuid();
                }

                profile_manager->Login(xuid, i - 1);
                LoadProfileIcon(xuid);

                // Release resources
                if (current_slot_xuid) {
                  LoadProfileIcon(current_slot_xuid);
                }
              }
            }
            ImGui::EndMenu();
          }
          ImGui::EndDisabled();
        } else {
          if (ImGui::MenuItem("Logout")) {
            profile_manager->Logout(user_index);
            LoadProfileIcon(xuid);
          }
        }

        if (ImGui::MenuItem("Modify")) {
          // Gamercard might outlive this dialog, so don't capture this.
          auto on_icon_change = [pending_icon_reloads =
                                     std::weak_ptr(pending_icon_reloads_),
                                 xuid]() {
            if (const auto pending = pending_icon_reloads.lock()) {
              pending->insert(xuid);
            }
          };

          new kernel::xam::ui::GamercardUI(
              emulator_window_->window(), emulator_window_->imgui_drawer(),
              emulator_window_->emulator()->kernel_state(), xuid,
              on_icon_change);
        }

        if (ImGui::BeginMenu("Copy")) {
          if (ImGui::MenuItem("Gamertag")) {
            ImGui::SetClipboardText(account.GetGamertagString().c_str());
          }

          if (ImGui::MenuItem("XUID")) {
            ImGui::SetClipboardText(fmt::format("{:016X}", xuid).c_str());
          }

          if (account.IsLiveEnabled()) {
            if (ImGui::MenuItem("XUID Online")) {
              ImGui::SetClipboardText(
                  fmt::format("{:016X}", account.xuid_online.get()).c_str());
            }
          }

          ImGui::EndMenu();
        }

        const bool is_signedin = profile_manager->GetProfile(xuid) != nullptr;
        ImGui::BeginDisabled(!is_signedin);
        if (ImGui::MenuItem("Show Played Titles")) {
          new kernel::xam::ui::TitleListUI(
              emulator_window_->imgui_drawer(), next_window_position,
              profile_manager->GetProfile(user_index));
        }
        ImGui::EndDisabled();

        if (ImGui::MenuItem("Show Content Directory")) {
          const auto path = profile_manager->GetProfileContentPath(
              xuid, emulator_window_->emulator()->kernel_state()->title_id());

          if (!std::filesystem::exists(path)) {
            std::filesystem::create_directories(path);
          }

          std::thread path_open(LaunchFileExplorer, path);
          path_open.detach();
        }

        ImGui::BeginDisabled(!account.IsLiveEnabled());
        if (ImGui::BeginMenu("XStorage Content")) {
          const auto emulator = emulator_window_->emulator();
          const auto filesystem = emulator->kernel_state()->file_system();

          xe::vfs::HostPathEntry* entry = nullptr;
          std::string symlink_path;

          if (ImGui::MenuItem("Show Game Data")) {
            if (emulator->is_title_open()) {
              symlink_path =
                  fmt::format("XSTORAGE:user\\{:016X}\\title\\{:08X}",
                              account.xuid_online.get(), emulator->title_id());

            } else {
              symlink_path = fmt::format("XSTORAGE:user\\{:016X}",
                                         account.xuid_online.get());
            }
          }

          ImGui::BeginDisabled(!emulator->is_title_open());
          if (ImGui::MenuItem("Show Game Clips")) {
            symlink_path =
                fmt::format("XSTORAGE:clips\\title\\{:08X}\\{:016X}",
                            emulator->title_id(), account.xuid_online.get());
          }
          ImGui::EndDisabled();

          if (!symlink_path.empty()) {
            entry = reinterpret_cast<xe::vfs::HostPathEntry*>(
                filesystem->ResolvePath(symlink_path));

            if (entry) {
              std::thread path_open(LaunchFileExplorer, entry->host_path());
              path_open.detach();
            }
          }

          ImGui::EndMenu();
        }
        ImGui::EndDisabled();

        if (!emulator_window_->emulator()->is_title_open()) {
          ImGui::Separator();

          if (account.IsLiveEnabled()) {
            ImGui::BeginDisabled(!is_signedin);
            if (ImGui::BeginMenu("Convert to Offline Profile")) {
              ImGui::BeginTooltip();
              ImGui::TextUnformatted(
                  fmt::format(
                      "You're about to convert profile: {} (XUID: {:016X}) "
                      "to an offline profile. Are you sure?",
                      account.GetGamertagString(), xuid)
                      .c_str());
              ImGui::EndTooltip();

              if (ImGui::MenuItem("Yes, convert it!")) {
                profile_manager->ConvertToOfflineProfile(xuid);
                ImGui::EndMenu();
                ImGui::EndDisabled();
                ImGui::EndPopup();
                return false;
              }

              ImGui::EndMenu();
            }
            ImGui::EndDisabled();
          } else {
            ImGui::BeginDisabled(!is_signedin);
            if (ImGui::BeginMenu("Convert to Xbox Live-Enabled Profile")) {
              ImGui::BeginTooltip();
              ImGui::TextUnformatted(
                  fmt::format(
                      "You're about to convert profile: {} (XUID: {:016X}) "
                      "to an Xbox Live-Enabled profile. Are you sure?",
                      account.GetGamertagString(), xuid)
                      .c_str());
              ImGui::EndTooltip();

              if (ImGui::MenuItem("Yes, convert it!")) {
                profile_manager->ConvertToXboxLiveEnabledProfile(xuid);
                ImGui::EndMenu();
                ImGui::EndDisabled();
                ImGui::EndPopup();
                return false;
              }

              ImGui::EndMenu();
            }
            ImGui::EndDisabled();
          }

          if (ImGui::BeginMenu("Delete Profile")) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(
                fmt::format(
                    "You're about to delete profile: {} (XUID: {:016X}). "
                    "This will remove all data assigned to this profile "
                    "including savefiles. Are you sure?",
                    account.GetGamertagString(), xuid)
                    .c_str());
            ImGui::EndTooltip();

            if (ImGui::MenuItem("Yes, delete it!")) {
              profile_manager->DeleteProfile(xuid);
              ImGui::EndMenu();
              ImGui::EndPopup();
              return false;
            }

            ImGui::EndMenu();
          }
        }
        ImGui::EndPopup();
      }
      return true;
    };

    if (!kernel::xam::xeDrawProfileContent(
            imgui_drawer(), xuid, user_index, &account, profile_icon,
            context_menu_fun, [=, this]() { LoadProfileIcon(xuid); },
            &selected_xuid_)) {
      ImGui::PopID();
      ImGui::EndChild();
      ImGui::End();
      return;
    }

    ImGui::PopID();
    ImGui::Separator();
  }

  ImGui::EndChild();

  ImGui::Spacing();

  if (ImGui::Button("Create Profile")) {
    new kernel::xam::ui::CreateProfileUI(emulator_window_->imgui_drawer(),
                                         emulator_window_->emulator());
  }

  ImGui::End();
}

bool ManagerDialog::DrawEntry(const char* id, const char* title,
                              const char* description, bool enabled,
                              const char* error) {
  ImFont* font = ImGui::GetFont();
  const float font_size = ImGui::GetFontSize();
  const float title_size = font_size * kEntryTitleScale;
  const float line_spacing = 2.0f;

  const ImVec2 size =
      ImVec2(ImGui::GetContentRegionAvail().x,
             title_size + line_spacing + font_size + kEntryPadding.y * 2);

  ImGui::BeginDisabled(!enabled);
  const bool pressed =
      ImGui::InvisibleButton(id, size, ImGuiButtonFlags_EnableNav);
  const bool hovered = ImGui::IsItemHovered();
  const bool active = ImGui::IsItemActive();
  ImGui::EndDisabled();

  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();

  ImDrawList* draw_list = ImGui::GetWindowDrawList();

  ImU32 card_color = kernel::xam::ui::kFriendCardColor;
  if (enabled && active) {
    card_color = kernel::xam::ui::kFriendCardActiveColor;
  } else if (enabled && hovered) {
    card_color = kernel::xam::ui::kFriendCardHoveredColor;
  }

  draw_list->AddRectFilled(min, max, card_color,
                           kernel::xam::ui::kFriendCardRounding);

  const float alpha = enabled ? 1.0f : ImGui::GetStyle().DisabledAlpha;

  const ImVec2 title_pos =
      ImVec2(min.x + kEntryPadding.x, min.y + kEntryPadding.y);
  draw_list->AddText(font, title_size, title_pos,
                     ImGui::GetColorU32(ImGuiCol_Text, alpha), title);

  draw_list->AddText(
      font, font_size,
      ImVec2(title_pos.x, title_pos.y + title_size + line_spacing),
      ImGui::GetColorU32(ImGuiCol_TextDisabled, alpha), description);

  if (error) {
    draw_list->AddRect(min, max, kErrorColor,
                       kernel::xam::ui::kFriendCardRounding, 2.0f);

    const ImVec2 error_size =
        font->CalcTextSizeA(font_size, FLT_MAX, 0.0f, error);

    // Vertically align the error with the title.
    draw_list->AddText(font, font_size,
                       ImVec2(max.x - kEntryPadding.x - error_size.x,
                              title_pos.y + (title_size - font_size) * 0.5f),
                       kErrorColor, error);
  }

  return pressed && enabled;
}

void ManagerDialog::Initialize(ui::ImGuiDrawer* imgui_drawer,
                               uint32_t user_index) {
  emulator_ = emulator_window_->emulator();

  const auto profile =
      emulator_->kernel_state()->xam_state()->GetUserProfile(user_index);

  if (!profile) {
    return;
  }

  // If title is open we can assume periodic maintenance is running, therefore
  // presence sync isn't needed.
  if (!emulator_->is_title_open()) {
    friends_ui_args_.friends_presence_sync =
        emulator_->kernel_state()->presence_manager()->SyncPresenceAsync(
            profile->xuid());
  } else {
    friends_ui_args_.friends_presence =
        emulator_->kernel_state()
            ->xam_state()
            ->presence_manager()
            ->GetFriendsPresenceSorted(profile->xuid());
  }

  friends_ui_args_.immediate_gamerpics =
      emulator_->GetXboxLiveAPI()->GetFriendsGamerpicsAsync(profile->xuid(),
                                                            imgui_drawer);
}

void ManagerDialog::OnDraw(ImGuiIO& io) {
  if (!manager_opened_) {
    manager_opened_ = true;
    ImGui::OpenPopup("Manager");

    sessions_args.filter_own = true;
  }

  // Add profile dropdown selector?
  const uint32_t user_index = 0;

  auto profile =
      emulator_->kernel_state()->xam_state()->GetUserProfile(user_index);

  const bool is_signed_in = profile != nullptr;
  const bool is_connected = emulator_->GetXboxLiveAPI()->IsConnectedToServer();
  const bool is_xuid_mismatched =
      emulator_->GetXboxLiveAPI()->IsXUIDMismatched();

  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImVec2 center = viewport->GetCenter();

  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSizeConstraints(ImVec2(kWidth, -1), ImVec2(kWidth, -1));
  if (ImGui::BeginPopupModal("Manager", &manager_opened_,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    // Only close when the manager itself is focused, not one of its popups.
    if (ImGui::IsWindowFocused() &&
        ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_GamepadFaceRight, false)) {
      manager_opened_ = false;
      ImGui::CloseCurrentPopup();
    }

    // Header: signed in profile on the left, server status on the right.
    {
      if (is_signed_in) {
        ImGui::TextDisabled("Signed in as");
        ImGui::SameLine();
        ImGui::TextUnformatted(profile->name().c_str());
      } else {
        ImGui::TextColored(ImColor(kWarningColor), "Not signed in");
      }

      const char* server_label = is_connected ? "Connected" : "Offline";
      const ImU32 server_color = is_connected ? kOnlineColor : kErrorColor;

      ImGui::SameLine();
      ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(),
                                    ImGui::GetCursorPosX() +
                                        ImGui::GetContentRegionAvail().x -
                                        ImGui::CalcTextSize(server_label).x));
      ImGui::TextColored(ImColor(server_color), "%s", server_label);
    }

    ImGui::Separator();
    ImGui::Spacing();

    if (DrawEntry("##Friends", "Friends",
                  is_signed_in ? "View friends and their presence."
                               : "Sign in to a profile to view friends.",
                  is_signed_in)) {
      friends_ui_args_.content_args.friends_open = true;
      friends_ui_args_.content_args.first_draw = true;

      if (is_connected) {
        friends_ui_args_.content_args.filter_offline = true;
      }

      ImGui::OpenPopup("Friends");
    }

    const char* sessions_description = "Browse your hosted sessions.";
    if (!is_signed_in) {
      sessions_description = "Sign in to a profile to view sessions.";
    } else if (!is_connected) {
      sessions_description = "Requires a connection to the server.";
    }

    if (DrawEntry("##Sessions", "Sessions", sessions_description,
                  is_signed_in && is_connected)) {
      sessions_args.sessions_open = true;
      ImGui::OpenPopup("Sessions");
    }

    if (DrawEntry("##UPnPAndPorts", "UPnP and Ports",
                  "Toggle UPnP and check port forwarding.", true)) {
      upnp_and_ports_args.first_draw = true;
      upnp_and_ports_args.dialog_open = true;
      ImGui::OpenPopup("UPnP and Ports");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (DrawEntry("##DeleteProfiles", "Delete Netplay Profiles",
                  is_xuid_mismatched
                      ? "Delete profiles to fix the XUID mismatch."
                      : "Remove profiles registered with the server.",
                  true, is_xuid_mismatched ? "XUID mismatch" : nullptr)) {
      ImGui::OpenPopup("Delete Profiles");
    }

    if (!sessions_args.sessions_open) {
      sessions_args.first_draw = false;
      sessions_args.refresh_sessions_sync = true;
      sessions.clear();
    }

    xeDrawFriendsUI(imgui_drawer(), profile, friends_ui_args_);

    xeDrawSessionsContent(imgui_drawer(), profile, sessions_args, &sessions);

    if (!deletion_args.deleted_profiles_open) {
      deletion_args.first_draw = false;
      deleted_profiles = {};
    }

    bool open_deleted_profiles = false;

    float btn_height = 25;
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(225, -1), ImVec2(225, -1));
    if (ImGui::BeginPopupModal("Delete Profiles", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      if (ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_GamepadFaceRight, false)) {
        ImGui::CloseCurrentPopup();
      }

      float btn_width = (ImGui::GetContentRegionAvail().x * 0.5f) -
                        (ImGui::GetStyle().ItemSpacing.x * 0.5f);
      ImVec2 btn_size = ImVec2(btn_width, btn_height);

      const std::string desc = "Are you sure?";
      const std::string desc2 = "You will be signed out.";

      ImVec2 desc_size = ImGui::CalcTextSize(desc.c_str());
      ImVec2 desc2_size = ImGui::CalcTextSize(desc2.c_str());

      ImGui::SetCursorPosX((ImGui::GetWindowWidth() - desc_size.x) * 0.5f);
      ImGui::Text(desc.c_str());

      if (is_signed_in) {
        ImGui::Spacing();

        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - desc2_size.x) * 0.5f);
        ImGui::Text(desc2.c_str());
      }

      ImGui::Separator();

      if (ImGui::Button("Yes", btn_size)) {
        if (is_signed_in) {
          std::map<uint8_t, uint64_t> xuids;

          kernel::xam::XamState* xam_state =
              emulator_window_->emulator()->kernel_state()->xam_state();

          for (uint32_t i = 0; i < XUserMaxUserCount; i++) {
            if (xam_state->IsUserSignedIn(i)) {
              xuids[i] = xam_state->GetUserProfile(i)->xuid();
            }
          }

          xam_state->profile_manager()->LogoutMultiple(xuids);
        }

        deleted_profiles = emulator_->GetXboxLiveAPI()->DeleteMyProfiles();

        open_deleted_profiles = true;

        ImGui::CloseCurrentPopup();
      }

      ImGui::SameLine();

      if (ImGui::Button("Cancel", btn_size)) {
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    }

    if (open_deleted_profiles) {
      emulator_->GetXboxLiveAPI()->SetXUIDMismatch(false);

      deletion_args.deleted_profiles_open = true;
      ImGui::OpenPopup("Deleted Profiles");
    }

    xe::kernel::xam::xeDrawMyDeletedProfiles(imgui_drawer(), deletion_args,
                                             &deleted_profiles);

    if (!upnp_and_ports_args.dialog_open) {
      upnp_and_ports_args.first_draw = false;
    }

    xe::kernel::xam::xeDrawUPnPAndPorts(imgui_drawer(), upnp_and_ports_args,
                                        emulator_->GetUPnP());

    ImGui::EndPopup();
  }

  if (!manager_opened_) {
    Close();
    ImGui::CloseCurrentPopup();
    emulator_window_->ToggleFriendsDialog();
  }
}

}  // namespace app
}  // namespace xe
