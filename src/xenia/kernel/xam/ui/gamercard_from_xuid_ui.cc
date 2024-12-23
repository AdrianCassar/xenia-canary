/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/ui/gamercard_from_xuid_ui.h"
#include "xenia/emulator.h"
#include "xenia/kernel/XLiveAPI.h"
#include "xenia/ui/imgui_host_notification.h"

namespace xe {
namespace kernel {
namespace xam {
namespace ui {

GamercardFromXUIDUI::GamercardFromXUIDUI(xe::ui::ImGuiDrawer* imgui_drawer,
                                         const uint64_t xuid,
                                         UserProfile* profile)
    : XamDialog(imgui_drawer),
      xuid_(xuid),
      profile_(profile),
      presence_({}),
      title_("Gamercard") {
  is_self = xuid_ == profile_->xuid() || xuid_ == profile_->GetOnlineXUID();

  if (!is_self) {
    assert_true(IsOnlineXUID(xuid_));
  }

  if (!XLiveAPI::IsConnectedToServer()) {
    if (is_self) {
      presence_.Gamertag(profile_->name());

      presence_.RichPresence(
          xe::to_utf16(xe::string_util::trim(profile_->GetPresenceString())));

      presence_.TitleID(fmt::format("{:08X}", kernel_state()->title_id()));
    } else if (!is_self) {
      // Cached friend presence
      X_ONLINE_FRIEND friend_info_ = {};
      are_friends = profile_->IsFriend(xuid_, &friend_info_);

      presence_.Gamertag("Xenia User");
      presence_.RichPresence(xe::to_utf16("Unknown"));
      presence_.XUID(friend_info_.xuid);

      if (friend_info_.title_id) {
        presence_.TitleID(fmt::format("{:08X}", friend_info_.title_id.get()));
      }
    }
  } else {
    std::vector<uint64_t> player_xuid = {xuid_};
    const auto presences = XLiveAPI::GetFriendsPresence(player_xuid);

    if (!presences->PlayersPresence().empty()) {
      presence_ = presences->PlayersPresence().front();

      if (is_self) {
        presence_.RichPresence(
            xe::to_utf16(xe::string_util::trim(profile_->GetPresenceString())));
      }
    }
  }
}

void GamercardFromXUIDUI::OnDraw(ImGuiIO& io) {
  bool first_draw = false;
  if (!has_opened_) {
    ImGui::OpenPopup(title_.c_str());
    has_opened_ = true;
    first_draw = true;
  }

  if (ImGui::BeginPopupModal(title_.c_str(), nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    if (is_self) {
      const uint8_t user_index =
          kernel_state()->xam_state()->GetUserIndexAssignedToProfileFromXUID(
              xuid_);
      const auto account =
          kernel_state()->xam_state()->profile_manager()->GetAccount(
              profile_->xuid());

      xeDrawProfileContent(imgui_drawer(), profile_->xuid(), user_index,
                           account, nullptr, nullptr, nullptr, nullptr);
      ImGui::NewLine();
    } else if (!presence_.Gamertag().empty()) {
      ImGui::TextUnformatted(presence_.Gamertag().c_str());
      ImGui::NewLine();
    }

    if (!presence_.TitleID().empty()) {
      const uint32_t title_id =
          string_util::from_string<uint32_t>(presence_.TitleID(), true);

      if (title_id && title_id == kernel_state()->title_id()) {
        ImGui::TextUnformatted(
            fmt::format("Game: {}", kernel_state()->emulator()->title_name())
                .c_str());
      } else {
        ImGui::TextUnformatted(
            fmt::format("Title ID: {}", presence_.TitleID()).c_str());
      }
    }

    if (!presence_.RichPresence().empty()) {
      ImGui::TextUnformatted(
          fmt::format("Status: {}", xe::to_utf8(presence_.RichPresence()))
              .c_str());
    }

    if (presence_.SessionID()) {
      ImGui::TextUnformatted(
          fmt::format("Session ID: {:016X}", presence_.SessionID().get())
              .c_str());
    }

    ImGui::TextUnformatted(fmt::format("Online XUID: {:016X}", xuid_).c_str());

    if (!is_self) {
      are_friends = profile_->IsFriend(xuid_);

      const uint32_t user_index = 0;

      ImGui::BeginDisabled(are_friends);
      if (ImGui::Button("Add Friend")) {
        if (profile_->AddFriendFromXUID(xuid_)) {
          XLiveAPI::AddFriend(xuid_);
          kernel_state()->BroadcastNotification(
              kXNotificationFriendsFriendAdded, user_index);
        }

        std::string description =
            !presence_.Gamertag().empty() ? presence_.Gamertag() : "Success";

        kernel_state()
            ->emulator()
            ->display_window()
            ->app_context()
            .CallInUIThread([&]() {
              new xe::ui::HostNotificationWindow(imgui_drawer(), "Added Friend",
                                                 description, 0);
            });
      }

      ImGui::EndDisabled();
      ImGui::SameLine();

      ImGui::BeginDisabled(!are_friends);
      if (ImGui::Button("Remove Friend")) {
        if (profile_->RemoveFriend(xuid_)) {
          XLiveAPI::RemoveFriend(xuid_);
          kernel_state()->BroadcastNotification(
              kXNotificationFriendsFriendRemoved, user_index);

          std::string description =
              !presence_.Gamertag().empty() ? presence_.Gamertag() : "Success";

          kernel_state()
              ->emulator()
              ->display_window()
              ->app_context()
              .CallInUIThread([&]() {
                new xe::ui::HostNotificationWindow(
                    imgui_drawer(), "Removed Friend", description, 0);
              });
        }
      }

      ImGui::EndDisabled();
      ImGui::SameLine();
    }

    if (ImGui::Button("Exit")) {
      ImGui::CloseCurrentPopup();
      Close();
    }
  }
  ImGui::EndPopup();
}

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe
