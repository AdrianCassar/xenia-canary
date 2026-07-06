/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2025 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/ui/gamercard_from_xuid_ui.h"
#include "xenia/kernel/XLiveAPI.h"

namespace xe {
namespace kernel {
namespace xam {
namespace ui {

GamercardFromXUIDUI::GamercardFromXUIDUI(xe::ui::ImGuiDrawer* imgui_drawer,
                                         const uint64_t xuid,
                                         UserProfile* profile)
    : XamDialog(imgui_drawer), xuid_(xuid), profile_(profile) {
  is_self = xuid_ == profile_->xuid() || xuid_ == profile_->GetOnlineXUID();

  if (!is_self) {
    assert_true(IsOnlineXUID(xuid_));
  }

  if (!kernel_state()->GetXboxLiveAPI()->IsConnectedToServer()) {
    if (is_self) {
      presence_.Gamertag(profile_->name());
      presence_.RichPresence(profile_->GetPresenceString());
      presence_.XUID(profile_->GetOnlineXUID());
      presence_.TitleID(fmt::format("{:08X}", kernel_state()->title_id()));
    } else if (!is_self) {
      // Cached friend presence
      const auto friend_info =
          kernel_state()->friends_manager()->GetFriend(profile_->xuid(), xuid);

      presence_.Gamertag("Xenia User");
      presence_.RichPresence(xe::to_utf16("Unknown"));

      if (friend_info.has_value()) {
        are_friends = true;

        presence_.XUID(friend_info->xuid);

        if (friend_info->title_id) {
          presence_.TitleID(fmt::format("{:08X}", friend_info->title_id.get()));
        }
      }
    }
  } else {
    const auto presences =
        kernel_state()->presence_manager()->GetFriendsPresence(profile_->xuid(),
                                                               {xuid_});

    immediate_gamerpic_ = std::async(std::launch::async, [xuid,
                                                          imgui_drawer]() {
      const auto gamerpic =
          kernel_state()->GetXboxLiveAPI()->GetUserGamerpicTile(xuid, false);

      std::shared_ptr<xe::ui::ImmediateTexture> shared_gamerpic =
          std::move(imgui_drawer->LoadImGuiIcon({gamerpic}));

      return shared_gamerpic;
    });

    presence_.XUID(xuid_);

    if (!presences->PlayersPresence().empty()) {
      presence_ = presences->PlayersPresence().front();

      if (is_self) {
        presence_.RichPresence(profile_->GetPresenceString());
      }
    }
  }

  if (is_self) {
    const auto gamerpic = kernel_state()
                              ->xam_state()
                              ->GetUserProfile(profile_->xuid())
                              ->GetProfileIcon(XTileType::kGamerTile);
    immediate_gamerpic_ =
        std::async(std::launch::async, [gamerpic, imgui_drawer]() {
          std::shared_ptr<xe::ui::ImmediateTexture> shared_gamerpic =
              std::move(imgui_drawer->LoadImGuiIcon({gamerpic}));

          return shared_gamerpic;
        });
  }
}

void GamercardFromXUIDUI::OnDraw(ImGuiIO& io) {
  if (!dialog_open) {
    dialog_open = true;
    ImGui::OpenPopup(title_.c_str());
  }

  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImVec2 center = viewport->GetCenter();

  std::shared_ptr<xe::ui::ImmediateTexture> gamerpic_texture = {};

  if (immediate_gamerpic_.valid()) {
    if (immediate_gamerpic_.wait_for(0s) == std::future_status::ready) {
      gamerpic_texture = immediate_gamerpic_.get();
    }
  }

  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal(title_.c_str(), &dialog_open,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    if (ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_GamepadFaceRight, false)) {
      ImGui::CloseCurrentPopup();
    }

    friend_presence_ = presence_.GetFriendPresence();

    xeDrawFriendContent(imgui_drawer(), profile_, gamerpic_texture,
                        friend_presence_, nullptr, nullptr);

    ImGui::EndPopup();
  }

  if (!dialog_open) {
    Close();
  }
}

}  // namespace ui
}  // namespace xam
}  // namespace kernel
}  // namespace xe
