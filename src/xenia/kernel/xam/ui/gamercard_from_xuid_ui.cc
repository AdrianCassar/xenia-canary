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

      presence_.XUID(profile_->GetOnlineXUID());

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
  if (!card_opened) {
    card_opened = true;
    ImGui::OpenPopup(title_.c_str());
  }

  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImVec2 center = viewport->GetCenter();

  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal(title_.c_str(), &card_opened,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    if (is_self) {
      const uint8_t user_index =
          kernel_state()->xam_state()->GetUserIndexAssignedToProfileFromXUID(
              xuid_);
      const auto account =
          kernel_state()->xam_state()->profile_manager()->GetAccount(
              profile_->xuid());

      const auto gamer_icon = kernel_state()
                                  ->xam_state()
                                  ->GetUserProfile(profile_->xuid())
                                  ->GetProfileIcon(XTileType::kGamerTile);

      xe::ui::ImmediateTexture* icon_texture =
          imgui_drawer()->LoadImGuiIcon(gamer_icon).release();

      kernel::xam::xeDrawProfileContent(imgui_drawer(), profile_->xuid(),
                                        user_index, account, icon_texture,
                                        nullptr, {}, nullptr);
      ImGui::Separator();
      ImGui::Spacing();
    }

    xeDrawFriendContent(imgui_drawer(), profile_, presence_, nullptr, nullptr);

    ImGui::EndPopup();
  }

  if (!card_opened) {
    Close();
  }
}

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe
