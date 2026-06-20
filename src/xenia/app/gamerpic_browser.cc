/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <algorithm>
#include <cfloat>
#include <ranges>
#include <span>
#include <string>
#include <thread>

#include <third_party/stb/stb_image.h>

#include "xenia/app/emulator_window.h"
#include "xenia/app/gamerpic_browser.h"
#include "xenia/base/jpeg_utils.h"
#include "xenia/base/logging.h"
#include "xenia/base/png_utils.h"
#include "xenia/base/system.h"
#include "xenia/kernel/XLiveAPI.h"
#include "xenia/kernel/util/friends_util.h"
#include "xenia/kernel/xam/user_data.h"

using namespace std::chrono_literals;

namespace xe {
namespace app {

auto item_getter = [](void* data, int idx) {
  std::vector<std::string>& vector =
      *static_cast<std::vector<std::string>*>(data);
  return vector.at(idx).c_str();
};

// Blue buttons used throughout the browser and its popups.
void PushButtonStyle() {
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.16f, 0.29f, 0.48f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                        ImVec4(0.26f, 0.59f, 0.98f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                        ImVec4(0.06f, 0.53f, 0.98f, 1.0f));
}

void PopButtonStyle() { ImGui::PopStyleColor(3); }

// Unlike std::async the returned future doesn't block when it's destroyed or
// replaced, so a slow or unreachable API can't stall the UI thread.
// func must not capture the browser as it may outlive it.
template <typename F>
std::future<std::invoke_result_t<F>> RunDetachedAsync(F&& func) {
  using Result = std::invoke_result_t<F>;

  auto promise = std::make_shared<std::promise<Result>>();
  auto future = promise->get_future();

  std::thread([promise, func = std::forward<F>(func)]() mutable {
    try {
      promise->set_value(func());
    } catch (...) {
      promise->set_exception(std::current_exception());
    }
  }).detach();

  return future;
}

// Copy-on-write update which retries if another thread published a new map in
// the meantime, so concurrent loaders can't discard each other's entries.
// Discarded entries would destroy textures which may still be drawn.
template <typename Map, typename Modify>
void UpdateAtomicMap(std::atomic<std::shared_ptr<const Map>>& atomic_map,
                     Modify modify) {
  std::shared_ptr<const Map> current =
      atomic_map.load(std::memory_order_acquire);
  std::shared_ptr<const Map> next;

  do {
    auto updated = std::make_shared<Map>(current ? *current : Map{});
    modify(*updated);
    next = std::move(updated);
  } while (!atomic_map.compare_exchange_weak(
      current, next, std::memory_order_acq_rel, std::memory_order_acquire));
}

void TitleGamerpicBrowser::Initialize() {
  kernel_state_ = emulator_window_->emulator()->kernel_state();

  for (uint32_t slot = 0; slot < XUserMaxUserCount; slot++) {
    if (kernel_state_->xam_state()->IsUserSignedIn(slot)) {
      const auto user_profile =
          kernel_state_->xam_state()->GetUserProfile(slot);

      xuids_.push_back(user_profile->xuid());
      profiles_.push_back(user_profile->name());
    }
  }

  if (!xuids_.empty()) {
    profile_ = kernel_state_->xam_state()->GetUserProfile(xuids_.front());
    picture_key_setting_ = GetGamerPictureKey();
    LoadGamerpic();
  } else {
    profile_icon_ = imgui_drawer()->GetNotificationIcon(XUserMaxUserCount);

    xuids_.push_back(0);
    profiles_.push_back("Sign-in required!");
  }

  title_images_ = std::make_shared<AtomicTitlesMap>();
  immediate_title_images_ = std::make_shared<AtomicImmediateTitlesMap>();

  title_gamerpics_ = std::make_shared<AtomicGamerpicsMap>();
  immediate_title_gamerpics_ =
      std::make_shared<AtomicImmediateTitleGamerpics>();

  supported_titles_result_ = GetSupportedTitlesAsync().share();
  dashboard_title_ = LoadTitleAsync(xe::kernel::kDashboardID).share();
  gamerpic_page_ = LoadFirstPage();
  page_changed_ = true;
}

void TitleGamerpicBrowser::OnClose() {
  if (load_dashboard_gamerpics_worker_thread_.has_value()) {
    load_dashboard_gamerpics_worker_thread_->request_stop();
    load_dashboard_gamerpics_worker_thread_ = std::nullopt;
  }

  CloseTitleImagesThreads();
  CloseGamerpicsThreads();
}

void TitleGamerpicBrowser::CleanupTitleImagesThreads() {
  load_title_images_worker_threads_.erase(
      std::remove_if(load_title_images_worker_threads_.begin(),
                     load_title_images_worker_threads_.end(),
                     [](const std::stop_source& source) {
                       return source.stop_requested();
                     }),
      load_title_images_worker_threads_.end());
}

void TitleGamerpicBrowser::CloseTitleImagesThreads() {
  for (auto& stop_source : load_title_images_worker_threads_) {
    stop_source.request_stop();
  }

  load_title_images_worker_threads_.clear();
}

void TitleGamerpicBrowser::CloseGamerpicsThreads() {
  if (load_gamerpics_worker_thread_.has_value()) {
    load_gamerpics_worker_thread_->request_stop();
    load_gamerpics_worker_thread_ = std::nullopt;
  }
}

void TitleGamerpicBrowser::OnDraw(ImGuiIO& io) {
  if (!titles_args_.browser_open) {
    titles_args_.first_draw = true;
    titles_args_.browser_open = true;

    ImGui::OpenPopup("Gamerpic Browser");
  }

  ImVec2 source_group_size = {};

  float btn_height_padding = ImGui::GetStyle().FramePadding.x * 2.5f;
  float btn_width_padding = ImGui::GetStyle().FramePadding.x * 5.0f;

  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImVec2 center = viewport->GetCenter();

  ImGui::SetNextWindowSizeConstraints(ImVec2(800, -1), ImVec2(800, -1));
  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal(
          "Gamerpic Browser", &titles_args_.browser_open,
          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
    // Applies to nested popups too, image buttons override it.
    PushButtonStyle();

    CleanupTitleImagesThreads();

    ProcessPageResult();
    ProcessOpenTitleResult();

    // If we have dashboard info then load dashboard gamerpics in the background
    if (!downloaded_dash_gamerpics) {
      const auto dashboard_title =
          GetDashboardTitleResult(dashboard_title_, max_retry_count_);

      if (dashboard_title.id == xe::kernel::kDashboardID) {
        downloaded_dash_gamerpics = true;
        LoadDashboardGamerpicsAsync(dashboard_title);
      }
    }

    if (ImGui::BeginTable("##Header", 2)) {
      ImGui::TableNextRow();

      ImGui::TableNextColumn();

      const uint32_t combo_item_width = 200;

      ImGui::SetNextItemWidth(combo_item_width);
      if (ImGui::Combo("##Profiles", &selected_profile_, item_getter,
                       reinterpret_cast<void*>(&profiles_),
                       static_cast<int>(profiles_.size()))) {
        uint64_t xuid = xuids_[selected_profile_];

        if (kernel_state_->xam_state()->IsUserSignedIn(xuid)) {
          profile_ = kernel_state_->xam_state()->GetUserProfile(xuid);
          picture_key_setting_ = GetGamerPictureKey();
          LoadGamerpic();
        }
      }

      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                            ImVec4(0.2f, 0.5f, 1.0f, 0.4f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                            ImVec4(0.1f, 0.4f, 0.9f, 0.8f));
      if (ImGui::ImageButton("##ProfileIcon",
                             reinterpret_cast<ImTextureID>(profile_icon_),
                             xe::ui::default_image_icon_size)) {
        if (downloaded_dash_gamerpics) {
          selected_title_gamerpics_ =
              GetDashboardTitleResult(dashboard_title_, max_retry_count_);
          gamerpic_args_.browser_open = true;
        }
      }
      ImGui::PopStyleColor(3);

      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(downloaded_dash_gamerpics
                              ? "Xbox 360 Dashboard"
                              : "Xbox 360 Dashboard (Unavailable)");
      }

      ImGui::TableNextColumn();
      std::string lbl_actual_source = "https://xboxgamer.pics/";

      ImVec2 actual_source_size =
          ImGui::CalcTextSize(lbl_actual_source.c_str());

      const ImVec2 pos = ImGui::GetCursorPos();

      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth() -
                           actual_source_size.x);

      // We need to use a separate thread otherwise window will freeze.
      if (ImGui::TextLink(lbl_actual_source.c_str())) {
        std::jthread open_link(LaunchWebBrowser, lbl_actual_source);
        open_link.detach();
      }
      ImGui::SetCursorPos(pos);

      ImGui::NewLine();

      std::string lbl_games_pre_page = "Games Per Page:";
      ImVec2 size_games_pre_page =
          ImGui::CalcTextSize(lbl_games_pre_page.c_str());

      const uint32_t item_width = 200;

      ImVec2 current_pos = ImGui::GetCursorPos();

      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetColumnWidth() -
                           (size_games_pre_page.x + item_width +
                            ImGui::GetStyle().ItemSpacing.x));
      ImGui::SetCursorPosY(ImGui::GetCursorPosY() +
                           ImGui::GetStyle().ItemSpacing.y);
      if (ImGui::BeginTable("##Select Type Table", 2)) {
        ImGui::TableSetupColumn("LabelCol", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("ComboCol", ImGuiTableColumnFlags_WidthFixed);

        ImGui::TableNextRow();

        ImGui::TableNextColumn();

        ImGui::Text(lbl_games_pre_page.c_str());

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(item_width);
        if (ImGui::Combo("##Games Per Page", &selected_per_page_,
                         per_page_options_.data(),
                         static_cast<int>(per_page_options_.size()))) {
          switch (selected_per_page_) {
            case 0:
              per_page_ = 20;
              break;
            case 1:
              per_page_ = 30;
              break;
            case 2:
              per_page_ = 40;
              break;
            case 3:
              per_page_ = 50;
              break;
          }

          gamerpic_page_ = LoadFirstPage();
        }

        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        ImGui::Text("Content Types:");

        std::vector<std::string> types = {};

        for (size_t i = 0; i < std::size(title_type_filter_state_); i++) {
          if (title_type_filter_state_[i]) {
            types.push_back(title_type_filter_[i]);
          }
        }

        std::string types_preview = "";
        bool first_type = true;

        for (size_t i = 0; i < std::size(title_type_filter_state_); i++) {
          if (title_type_filter_state_[i]) {
            types_preview += fmt::format("{}{}", first_type ? "" : ", ",
                                         title_type_filter_[i]);
            first_type = false;
          }
        }

        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(item_width);
        if (ImGui::BeginCombo("##Select Type", types_preview.c_str())) {
          bool current_filter_state[5] = {};

          std::copy(std::begin(title_type_filter_state_),
                    std::end(title_type_filter_state_),
                    std::begin(current_filter_state));

          for (int i = 0; i < std::size(title_type_filter_state_); i++) {
            ImGui::Checkbox(title_type_filter_.at(i),
                            &title_type_filter_state_[i]);
          }

          if (!std::ranges::equal(title_type_filter_state_,
                                  current_filter_state)) {
            title_type_filter_changed_ = true;
          }

          ImGui::EndCombo();
        } else {
          if (title_type_filter_changed_) {
            bool zero_state =
                std::ranges::all_of(title_type_filter_state_,
                                    [](bool state) { return state == false; });

            if (zero_state) {
              title_type_filter_state_[0] = true;
            }

            gamerpic_page_ = LoadFirstPage();

            title_type_filter_changed_ = false;
          }
        }

        ImGui::TableNextColumn();

        bool supported_title = false;
        uint32_t title_id = 0;

        std::string title_id_str(title_id_);

        if (title_id_str.size() == sizeof(title_id_) - 1) {
          if (IsValidHexString(title_id_)) {
            title_id = string_util::from_string<uint32_t>(title_id_, true);

            const auto& titles = GetSupportedTitlesResult(
                supported_titles_result_, max_retry_count_);

            supported_title = titles.contains(title_id);
          }
        }

        const bool opening_title = open_title_.valid();

        ImGui::BeginDisabled(!supported_title || opening_title);
        if (ImGui::Button(opening_title ? "Loading...###OpenTitle"
                                        : "Open Title###OpenTitle",
                          ImVec2(-1, 0))) {
          open_title_failed_ = false;

          if (title_id == xe::kernel::kDashboardID &&
              downloaded_dash_gamerpics) {
            selected_title_gamerpics_ =
                GetDashboardTitleResult(dashboard_title_, max_retry_count_);
            gamerpic_args_.browser_open = true;
          } else {
            const auto& titles = current_page_.GetTitles();

            // Check if page contains the current title lookup
            const auto title =
                std::find_if(titles.cbegin(), titles.cend(),
                             [title_id](xe::kernel::GameTitle game) {
                               return game.id == title_id;
                             });

            if (title != titles.cend()) {
              selected_title_gamerpics_ = *title;
              gamerpic_args_.browser_open = true;
            } else {
              // Opened by ProcessOpenTitleResult once loaded.
              open_title_ = LoadTitleAsync(title_id);
            }
          }
        }
        ImGui::EndDisabled();

        ImGui::SetItemTooltip("Show Gamerpics");

        ImGui::SetNextItemWidth(item_width);
        ImGui::TableNextColumn();

        DrawInputTextBoxWithHint("##SearchTitleGamerpics", "415608CB",
                                 title_id_, sizeof(title_id_),
                                 [this](std::span<const char> data) {
                                   return IsSupportedTitleID(data);
                                 });

        const std::string lbl_search_title_contexts = "##SearchTitleContexts";

        if (ImGui::IsItemFocused() &&
            ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_GamepadFaceDown, false)) {
          ImGui::OpenPopup(lbl_search_title_contexts.c_str());
        }

        if (ImGui::BeginPopupContextItem(lbl_search_title_contexts.c_str())) {
          if (ImGui::MenuItem("Paste")) {
            const char* clipboard = ImGui::GetClipboardText();

            if (clipboard) {
              xe::string_util::copy_truncating(title_id_, clipboard,
                                               sizeof(title_id_));
            }
          }

          if (!kernel_state_->emulator()->is_title_open()) {
            ImGui::Separator();
          }

          if (ImGui::MenuItem("Clear")) {
            memset(title_id_, 0, sizeof(title_id_));
          }

          if (kernel_state_->emulator()->is_title_open()) {
            ImGui::Separator();

            if (ImGui::MenuItem("Current Game")) {
              const std::string current_title =
                  fmt::format("{:08X}", kernel_state_->title_id());

              std::memcpy(title_id_, current_title.c_str(), sizeof(title_id_));
            }
          }

          ImGui::EndPopup();
        }

        ImGui::EndTable();
      }

      ImGui::SetCursorPos(current_pos);

      ImGui::EndTable();
    }

    ImGui::Separator();

    const float status_start_y = ImGui::GetCursorPosY();

    DrawStatus();

    // The table gives up the space taken by the status so the window height
    // stays constant instead of overflowing into a scrollbar.
    const float status_height = ImGui::GetCursorPosY() - status_start_y;

    const std::string table_id_str =
        fmt::format("##Page{}", current_page_.GetCurrentPage());

    ImVec2 table_size = ImVec2(-1, 425 - status_height);

    // Set page table focus every page load so navigation via gamepad works
    // continuously.
    if (page_changed_) {
      if (!gamerpic_args_.browser_open) {
        ImGui::SetNextWindowFocus();
      }

      page_changed_ = false;
    }

    if (ImGui::BeginTable(
            table_id_str.c_str(), columns_per_page_,
            ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedSame |
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_ScrollY,
            table_size)) {
      if (ImGui::IsWindowFocused()) {
        if (ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_GamepadR1, false)) {
          if (current_page_.GetNextPage() && !IsPageLoading()) {
            gamerpic_page_ = LoadNextPage();
          }
        }

        if (ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_GamepadL1, false)) {
          if (current_page_.GetPreviousPage() && !IsPageLoading()) {
            gamerpic_page_ = LoadPreviousPage();
          }
        }
      }

      for (const auto& title : current_page_.GetTitles()) {
        ImGui::TableNextColumn();

        xe::ui::ImmediateTexture* title_icon = nullptr;

        const auto immediate_images =
            immediate_title_images_->load(std::memory_order_acquire);

        // Caching the image improves performance
        if (immediate_images && immediate_images->contains(title.id)) {
          title_icon = immediate_images->at(title.id).get();
        } else {
          title_icon = imgui_drawer()->GetLoadingTileIcon();
        }

        // Height must be constant to ensure gamepad navigation between images
        // buttons is consistent.
        if (ImGui::BeginChild(
                fmt::format("##TitleContainer{:08X}", title.id).c_str(),
                ImVec2(-1, 30))) {
          ImGui::SetWindowFontScale(1.1f);
          ImGui::TextWrapped(title.name.c_str());
          ImGui::SetWindowFontScale(1.0f);
        }
        ImGui::EndChild();

        ImVec2 btn_gamerpic_centre = {};

        btn_gamerpic_centre.x = ImGui::GetCursorPosX() +
                                (ImGui::GetColumnWidth() / 2) -
                                (xe::ui::default_image_icon_size.x / 2);

        btn_gamerpic_centre.y =
            ImGui::GetCursorPosY() - (ImGui::GetStyle().ItemSpacing.y / 2);

        ImGui::SetCursorPos(btn_gamerpic_centre);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(0.2f, 0.5f, 1.0f, 0.4f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              ImVec4(0.1f, 0.4f, 0.9f, 0.8f));
        if (ImGui::ImageButton(
                fmt::format("##TitleIcon{:08X}", title.id).c_str(),
                reinterpret_cast<ImTextureID>(title_icon),
                xe::ui::default_image_icon_size)) {
          selected_title_gamerpics_ = title;
          gamerpic_args_.browser_open = true;
        }
        ImGui::PopStyleColor(3);

        std::string context_label =
            fmt::format("##TitleContext{:08X}", title.id);

        if (ImGui::IsItemFocused() &&
            ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_GamepadFaceLeft, false)) {
          ImGui::OpenPopup(context_label.c_str());
        }

        if (ImGui::BeginPopupContextItem(context_label.c_str())) {
          if (ImGui::MenuItem("Copy Title ID")) {
            ImGui::SetClipboardText(fmt::format("{:08X}", title.id).c_str());
          }

          ImGui::EndPopup();
        }
      }

      ImGui::EndTable();
    }

    std::string first_lbl = "<<";

    ImVec2 first_lbl_size = ImGui::CalcTextSize(first_lbl.c_str());
    ImVec2 first_btn_size = ImVec2(first_lbl_size.x + btn_width_padding,
                                   first_lbl_size.y + btn_height_padding);

    std::string prev_lbl = "Previous Page";

    ImVec2 prev_lbl_size = ImGui::CalcTextSize(prev_lbl.c_str());
    ImVec2 prev_btn_size = ImVec2(prev_lbl_size.x + btn_width_padding,
                                  prev_lbl_size.y + btn_height_padding);

    std::string page_pos_lbl =
        fmt::format("Page {}/{}", current_page_.GetCurrentPage(),
                    current_page_.GetTotalPages());

    ImVec2 page_pos_lbl_size = ImGui::CalcTextSize(page_pos_lbl.c_str());
    ImVec2 page_pos_btn_size = ImVec2(page_pos_lbl_size.x + btn_width_padding,
                                      page_pos_lbl_size.y + btn_height_padding);

    std::string next_lbl = "Next Page";

    ImVec2 next_lbl_size = ImGui::CalcTextSize(next_lbl.c_str());
    ImVec2 next_btn_size = ImVec2(next_lbl_size.x + btn_width_padding,
                                  next_lbl_size.y + btn_height_padding);

    std::string last_lbl = ">>";

    ImVec2 last_lbl_size = ImGui::CalcTextSize(last_lbl.c_str());
    ImVec2 last_btn_size = ImVec2(last_lbl_size.x + btn_width_padding,
                                  last_lbl_size.y + btn_height_padding);

    auto items_width = first_btn_size.x + prev_btn_size.x +
                       page_pos_btn_size.x + next_btn_size.x + last_btn_size.x +
                       (ImGui::GetStyle().ItemSpacing.x * 5);

    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - items_width) / 2);

    ImGui::BeginDisabled(!current_page_.GetPreviousPage());
    if (ImGui::Button(first_lbl.c_str(), first_btn_size)) {
      if (!IsPageLoading()) {
        gamerpic_page_ = LoadFirstPage();
      }
    }

    ImGui::SetItemTooltip("First Page");

    ImGui::SameLine();

    if (ImGui::Button(prev_lbl.c_str(), prev_btn_size)) {
      if (!IsPageLoading()) {
        gamerpic_page_ = LoadPreviousPage();
      }
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    std::string page_selection_title = "Page Selection";

    if (ImGui::Button(page_pos_lbl.c_str(), page_pos_btn_size)) {
      if (loaded_page_) {
        selected_page_pos_ = current_page_.GetCurrentPage();
      }

      page_selection_open_ = true;
      ImGui::OpenPopup(page_selection_title.c_str());
    }

    ImGui::SetItemTooltip("Page Selection");

    ImGui::SameLine();

    ImGui::BeginDisabled(!current_page_.GetNextPage());
    if (ImGui::Button(next_lbl.c_str(), next_btn_size)) {
      if (!IsPageLoading()) {
        gamerpic_page_ = LoadNextPage();
      }
    }

    ImGui::SameLine();

    if (ImGui::Button(last_lbl.c_str(), last_btn_size)) {
      if (!IsPageLoading()) {
        gamerpic_page_ = LoadLastPage();
      }
    }
    ImGui::EndDisabled();

    ImGui::SetItemTooltip("Last Page");

    // Manage opening gamerpics browser dialog
    if (gamerpic_args_.browser_open && !gamerpic_args_.first_draw) {
      gamerpic_args_.first_draw = true;
      gamerpic_args_.title_desc =
          fmt::format("{} | {:08X}", selected_title_gamerpics_.name,
                      selected_title_gamerpics_.id);

      LoadGamerpicsAsync(selected_title_gamerpics_);

      ImGui::OpenPopup(gamerpic_args_.title_desc.c_str());
      ImGui::SetNextWindowFocus();
    }

    if (gamerpic_args_.first_draw && !gamerpic_args_.browser_open) {
      gamerpic_args_.first_draw = false;
      selected_title_gamerpics_ = {};
    }

    DrawPageSelection(current_page_, page_selection_title);

    DrawGamerpicsBrowser(selected_title_gamerpics_, gamerpic_args_);

    PopButtonStyle();

    ImGui::EndPopup();
  }

  if (!titles_args_.browser_open) {
    Close();
    emulator_window_->ToggleGamerpicBrowserDialog();
  }
}

void TitleGamerpicBrowser::DrawPageSelection(
    xe::kernel::PageGamerpicsObjectJSON page, std::string title) {
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImVec2 center = viewport->GetCenter();

  const float popup_width = 340.0f;

  ImGui::SetNextWindowSizeConstraints(ImVec2(popup_width, 0),
                                      ImVec2(popup_width, FLT_MAX));
  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal(title.c_str(), &page_selection_open_,
                             ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_AlwaysAutoResize)) {
    const ImGuiStyle& style = ImGui::GetStyle();

    const uint32_t total_pages = page.GetTotalPages();
    const uint32_t current_page = page.GetCurrentPage();
    const bool has_pages = total_pages > 0;

    const uint32_t min_page = 1;
    const uint32_t max_page = std::max<uint32_t>(total_pages, 1);

    // Keep the selection within range at all times so the label, slider and
    // buttons always agree.
    selected_page_pos_ = std::clamp(selected_page_pos_, min_page, max_page);

    bool go_to_page = false;

    // Width of buttons when splitting the row evenly.
    const auto row_button_width = [&style](uint32_t count) {
      return (ImGui::GetContentRegionAvail().x -
              style.ItemSpacing.x * (count - 1)) /
             count;
    };

    if (!has_pages) {
      ImGui::TextDisabled("Page information is unavailable.");
    }

    ImGui::BeginDisabled(!has_pages);

    // Page X of N
    ImGui::Text("Page %u of %u", selected_page_pos_, total_pages);

    // Draggable slider for scrubbing through pages.
    ImGui::BeginDisabled(min_page == max_page);
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderScalar("##PageSlider", ImGuiDataType_U32, &selected_page_pos_,
                        &min_page, &max_page, "", ImGuiSliderFlags_AlwaysClamp);
    // Navigation focus (rather than keyboard focus, which would turn the slider
    // into a text input) so it can be stepped with a gamepad straight away.
    ImGui::SetItemDefaultFocus();
    ImGui::EndDisabled();

    if (ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false)) {
      go_to_page = true;
    }

    ImGui::EndDisabled();

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Actions
    const float action_width = row_button_width(2);
    const ImVec2 action_size = ImVec2(action_width, 0);

    ImGui::BeginDisabled(!has_pages || selected_page_pos_ == current_page);
    if (ImGui::Button(
            fmt::format("Go to Page {}###GoToPage", selected_page_pos_).c_str(),
            action_size)) {
      go_to_page = true;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    if (ImGui::Button("Cancel", action_size) ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false) ||
        ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false)) {
      ImGui::CloseCurrentPopup();
    }

    if (go_to_page && has_pages) {
      selected_page_pos_ = std::clamp(selected_page_pos_, min_page, max_page);

      if (selected_page_pos_ != current_page) {
        gamerpic_page_ = LoadPageAsync(selected_page_pos_);
      }

      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }
}

void TitleGamerpicBrowser::DrawGamerpicsBrowser(xe::kernel::GameTitle game,
                                                GamerpicBrowserArgs& args) {
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImVec2 center = viewport->GetCenter();

  ImGui::SetNextWindowSizeConstraints(ImVec2(500, -1), ImVec2(500, -1));
  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal(
          args.title_desc.c_str(), &args.browser_open,
          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize)) {
    ImVec2 table_size = ImVec2(-1, 435);

    if (ImGui::BeginTable(
            fmt::format("GamerpicsTable{:08X}", game.id).c_str(),
            title_gamerpics_columns_,
            ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedSame |
                ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_ScrollY,
            table_size)) {
      if (ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_GamepadFaceRight, false)) {
        ImGui::CloseCurrentPopup();
      }

      UpdateGamerpicIfRequested(game);

      for (const auto& gamerpic : game.gamerpics) {
        ImGui::TableNextColumn();

        xe::ui::ImmediateTexture* gamerpic_icon =
            imgui_drawer()->GetLoadingTileIcon();

        const auto immediate_gamerpics =
            immediate_title_gamerpics_->load(std::memory_order_acquire);

        bool gamerpic_downloaded = false;

        // Caching the image improves performance
        if (immediate_gamerpics && immediate_gamerpics->contains(game.id)) {
          if (immediate_gamerpics->at(game.id).contains(gamerpic.big_tile_id)) {
            gamerpic_icon =
                immediate_gamerpics->at(game.id).at(gamerpic.big_tile_id).get();

            gamerpic_downloaded = true;
          }
        }

        ImVec2 btn_gamerpic_centre = {};

        btn_gamerpic_centre.x = ImGui::GetCursorPosX() +
                                (ImGui::GetColumnWidth() / 2) -
                                (xe::ui::default_image_icon_size.x / 2);

        btn_gamerpic_centre.y =
            ImGui::GetCursorPosY() + (ImGui::GetStyle().ItemSpacing.y / 2);

        ImGui::SetCursorPos(btn_gamerpic_centre);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(0.2f, 0.5f, 1.0f, 0.4f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              ImVec4(0.1f, 0.4f, 0.9f, 0.8f));

        ImGui::BeginDisabled(profile_ == nullptr || !gamerpic_downloaded);
        if (ImGui::ImageButton(
                fmt::format("Gamerpic{:08X}", gamerpic.big_tile_id).c_str(),
                reinterpret_cast<ImTextureID>(gamerpic_icon),
                xe::ui::default_image_icon_size)) {
          // Check if gamerpic has changed, we don't want to broadcast and
          // download the gamerpics if it hasn't
          if (!IsCurrentGamerpic(game, gamerpic)) {
            new_gamerpic_ = gamerpic;
            update_gamerpic_ = true;
            small_gamerpic_ = RunDetachedAsync(
                [xlive_api = kernel_state_->GetXboxLiveAPI(),
                 title_id = game.id, tile_id = gamerpic.small_tile_id]() {
                  return xlive_api->DownloadGamerpicTile(title_id, tile_id);
                });
          } else {
            XELOGI(fmt::format("{}: Skiping Gamerpic Update.", game.name));
          }
        }
        ImGui::EndDisabled();

        ImGui::PopStyleColor(3);
      }

      ImGui::EndTable();
    }

    ImGui::EndPopup();
  } else {
    // If the gamerpics dialog closed then stop requesting gamerpics
    CloseGamerpicsThreads();
  }
}

TitleGamerpicBrowser::TitleIDInputState
TitleGamerpicBrowser::IsSupportedTitleID(std::span<const char> data) {
  std::string title_id_str = std::string(data.data());

  if (title_id_str.empty()) {
    return TitleIDInputState::EMPTY;
  }

  const auto& titles =
      GetSupportedTitlesResult(supported_titles_result_, max_retry_count_);

  if (titles.empty()) {
    return TitleIDInputState::EMPTY;
  }

  if (title_id_str.size() != 8) {
    return TitleIDInputState::EMPTY;
  }

  if (IsValidHexString(title_id_str)) {
    uint32_t title_id = string_util::from_string<uint32_t>(title_id_str, true);

    if (titles.contains(title_id)) {
      return TitleIDInputState::FOUND;
    } else {
      return TitleIDInputState::NOT_FOUND;
    }
  }

  return TitleIDInputState::NOT_FOUND;
}
void TitleGamerpicBrowser::DrawInputTextBoxWithHint(
    std::string label, std::string hint, char* buffer, size_t buffer_size,
    std::function<TitleIDInputState(std::span<char>)> on_input_change) {
  const ImVec2 input_field_pos = ImGui::GetCursorScreenPos();

  ImGui::InputTextWithHint(fmt::format("###{}", label).c_str(), hint.c_str(),
                           buffer, buffer_size);

  if (on_input_change) {
    const ImVec2 item_size = ImGui::GetItemRectSize();
    const ImVec2 end_pos = ImVec2(input_field_pos.x + item_size.x,
                                  input_field_pos.y + item_size.y);

    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    switch (on_input_change({buffer, buffer_size})) {
      case TitleIDInputState::NOT_FOUND:
        draw_list->AddRect(input_field_pos, end_pos, IM_COL32(255, 0, 0, 200),
                           0.0f, 0, 2.0f);
        break;
      case TitleIDInputState::FOUND:
        draw_list->AddRect(input_field_pos, end_pos, IM_COL32(0, 255, 0, 200),
                           0.0f, 0, 2.0f);
        break;
      case TitleIDInputState::EMPTY:
      default:
        break;
    }
  }
}

bool TitleGamerpicBrowser::IsValidHexString(std::string hex_string) {
  return std::ranges::all_of(hex_string,
                             [](unsigned char c) { return std::isxdigit(c); });
}

void TitleGamerpicBrowser::LoadGamerpic() {
  const auto gamer_icon =
      kernel_state_->xam_state()
          ->GetUserProfile(profile_->xuid())
          ->GetProfileIcon(xe::kernel::xam::XTileType::kGamerTile);

  if (!gamer_icon.empty()) {
    profile_icon_ = imgui_drawer()->LoadImGuiIcon(gamer_icon).release();
  } else {
    const uint8_t user_index =
        kernel_state_->xam_state()->GetUserIndexAssignedToProfileFromXUID(
            profile_->xuid());

    profile_icon_ = imgui_drawer()->GetNotificationIcon(user_index);
  }
}

std::optional<kernel::xam::UserSetting>
TitleGamerpicBrowser::GetGamerPictureKey() {
  const auto user_tracker = kernel_state_->xam_state()->user_tracker();

  const auto picture_key_setting = user_tracker->GetSetting(
      profile_, xe::kernel::kDashboardID,
      static_cast<uint32_t>(
          kernel::xam::UserSettingId::XPROFILE_GAMERCARD_PICTURE_KEY));

  return picture_key_setting;
}

bool TitleGamerpicBrowser::IsCurrentGamerpic(xe::kernel::GameTitle game,
                                             xe::kernel::Gamerpic gamerpic) {
  if (!picture_key_setting_.has_value()) {
    return false;
  }

  const std::u16string new_gamerpic_key =
      xe::to_utf16(fmt::format("{:08X}{:08X}{:08X}", game.id,
                               gamerpic.big_tile_id, gamerpic.small_tile_id));

  const std::u16string current_gamerpic_key =
      std::get<std::u16string>(picture_key_setting_.value().get_host_data());

  return current_gamerpic_key == new_gamerpic_key;
}

void TitleGamerpicBrowser::UpdateGamerpicIfRequested(
    xe::kernel::GameTitle game) {
  if (update_gamerpic_) {
    if (IsFutureReady(small_gamerpic_)) {
      bool updated = false;

      const auto gamerpics = title_gamerpics_->load(std::memory_order_acquire);

      // Ensure the big gamerpic is downloaded before attempting to
      // update profile.
      if (gamerpics && gamerpics->contains(game.id)) {
        if (gamerpics->at(game.id).contains(new_gamerpic_.big_tile_id)) {
          const auto& gamerpic_data =
              gamerpics->at(game.id).at(new_gamerpic_.big_tile_id);
          const auto user_tracker = kernel_state_->xam_state()->user_tracker();

          updated = user_tracker->UpdateUserGamerpic(
              profile_->xuid(), game.id, new_gamerpic_.big_tile_id,
              new_gamerpic_.small_tile_id, small_gamerpic_.get(),
              gamerpic_data);
        }
      }

      if (updated) {
        // XUIDs -> Title IDs -> Settings
        user_settingids_map settings = {};

        settings[profile_->GetOnlineXUID()][xe::kernel::kDashboardID].push_back(
            kernel::xam::UserSettingId::XPROFILE_GAMERCARD_PICTURE_KEY);

        // Profile settings are sent along with profile registration, therefore
        // it doesn't matter if this fails due to profile not existing on the
        // backend.
        if (!kernel_state_->GetXboxLiveAPI()->SetUsersSettings(settings)) {
          XELOGW("Updating Backend Gamerpic Setting Failed!");
        }

        new_gamerpic_ = {};
        small_gamerpic_ = {};

        picture_key_setting_ = GetGamerPictureKey();
        LoadGamerpic();
      } else {
        XELOGW("Updating Gamerpic Failed!");
      }

      update_gamerpic_ = false;
    }
  }
}

// Never blocks, a failed request is retried in the background.
std::set<uint32_t> TitleGamerpicBrowser::GetSupportedTitlesResult(
    std::shared_future<std::set<uint32_t>>& supported_title_future,
    uint32_t retry_count) {
  if (!IsFutureReady(supported_title_future)) {
    return {};
  }

  const std::set<uint32_t>& supported_titles = supported_title_future.get();

  // If the request failed then retry.
  if (supported_titles.empty()) {
    // Limit retry attempts to prevent continuously failed attempt.
    if (supported_titles_retry_count_ < retry_count) {
      supported_title_future = GetSupportedTitlesAsync().share();
      supported_titles_retry_count_++;
    }

    return {};
  }

  return supported_titles;
}

// Never blocks, a failed request is retried in the background.
xe::kernel::GameTitle TitleGamerpicBrowser::GetDashboardTitleResult(
    std::shared_future<xe::kernel::TitleGamerpicsObjectJSON>&
        dashboard_title_future,
    uint32_t retry_count) {
  if (!IsFutureReady(dashboard_title_future)) {
    return {};
  }

  const xe::kernel::GameTitle dash_title =
      dashboard_title_future.get().GetTitle();

  // If the request failed then retry.
  if (dash_title.id != xe::kernel::kDashboardID) {
    // Limit retry attempts to prevent continuously failed attempt.
    if (dashboard_title_retry_count_ < retry_count) {
      dashboard_title_future = LoadTitleAsync(xe::kernel::kDashboardID).share();
      dashboard_title_retry_count_++;
    }

    return {};
  }

  return dash_title;
}

bool TitleGamerpicBrowser::IsPageLoading() {
  // Futures are reset once their result is processed.
  return gamerpic_page_.valid();
}

void TitleGamerpicBrowser::ProcessPageResult() {
  if (!IsFutureReady(gamerpic_page_)) {
    return;
  }

  const PageResult page = gamerpic_page_.get();

  if (!page.has_value()) {
    page_load_failed_ = true;
    return;
  }

  current_page_ = page.value();
  page_changed_ = true;
  loaded_page_ = true;

  if (!current_page_.GetTitles().empty()) {
    LoadGameImagesAsync(current_page_);
  }
}

void TitleGamerpicBrowser::ProcessOpenTitleResult() {
  if (!IsFutureReady(open_title_)) {
    return;
  }

  const xe::kernel::GameTitle title = open_title_.get().GetTitle();

  if (!title.id) {
    XELOGW("{}: Failed to load title gamerpics!", __func__);
    open_title_failed_ = true;
    return;
  }

  selected_title_gamerpics_ = title;
  gamerpic_args_.browser_open = true;
}

void TitleGamerpicBrowser::RetryFailedRequests() {
  if (page_load_failed_) {
    gamerpic_page_ = LoadPageAsync(requested_page_pos_);
  }

  if (!downloaded_dash_gamerpics && IsFutureReady(dashboard_title_)) {
    dashboard_title_retry_count_ = 0;
    dashboard_title_ = LoadTitleAsync(xe::kernel::kDashboardID).share();
  }

  if (IsFutureReady(supported_titles_result_) &&
      supported_titles_result_.get().empty()) {
    supported_titles_retry_count_ = 0;
    supported_titles_result_ = GetSupportedTitlesAsync().share();
  }

  open_title_failed_ = false;
}

void TitleGamerpicBrowser::DrawStatus() {
  const bool dashboard_failed =
      !downloaded_dash_gamerpics && IsFutureReady(dashboard_title_) &&
      dashboard_title_retry_count_ >= max_retry_count_;

  const bool supported_titles_failed =
      IsFutureReady(supported_titles_result_) &&
      supported_titles_result_.get().empty() &&
      supported_titles_retry_count_ >= max_retry_count_;

  if (page_load_failed_ || open_title_failed_ || dashboard_failed ||
      supported_titles_failed) {
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                       "Unable to reach xboxgamer.pics, some content failed to "
                       "load.");

    ImGui::SameLine();

    if (ImGui::SmallButton("Retry")) {
      RetryFailedRequests();
    }
  } else if (IsPageLoading() && current_page_.GetTitles().empty()) {
    ImGui::TextDisabled("Loading...");
  }
}

std::future<std::set<uint32_t>>
TitleGamerpicBrowser::GetSupportedTitlesAsync() {
  return RunDetachedAsync([xlive_api = kernel_state_->GetXboxLiveAPI()]() {
    return xlive_api->GetSupportedGamerpicTitles();
  });
}

std::future<xe::kernel::TitleGamerpicsObjectJSON>
TitleGamerpicBrowser::LoadTitleAsync(uint32_t title_id) {
  return RunDetachedAsync(
      [xlive_api = kernel_state_->GetXboxLiveAPI(), title_id]() {
        return xlive_api->GetTitleGamerpic(title_id);
      });
}

std::future<TitleGamerpicBrowser::PageResult>
TitleGamerpicBrowser::LoadPageAsync(uint32_t page_pos) {
  loaded_page_ = false;
  page_load_failed_ = false;

  if (!page_pos) {
    page_pos = 1;
  }

  requested_page_pos_ = page_pos;

  std::vector<std::string> content_types = {};

  for (size_t i = 0; i < std::size(title_type_filter_state_); i++) {
    if (title_type_filter_state_[i]) {
      content_types.push_back(title_type_filter_[i]);
    }
  }

  const std::string content_types_query = xe::kernel::BuildCSVFromVector(
      content_types, std::size(title_type_filter_state_));

  // Request parameters are captured by value as the browser may be closed
  // before the request completes.
  return RunDetachedAsync([xlive_api = kernel_state_->GetXboxLiveAPI(),
                           page_pos, per_page = per_page_.load(),
                           query = utf8::lower_ascii(content_types_query)]() {
    return LoadPage(xlive_api, page_pos, per_page, query);
  });
}

// Pages aren't cached because per_page_ will change page responses.
TitleGamerpicBrowser::PageResult TitleGamerpicBrowser::LoadPage(
    xe::kernel::XLiveAPI* xlive_api, uint32_t page_pos, uint32_t per_page,
    std::string content_types_query) {
  PageResult responce =
      xlive_api->GetGamerpicPage(page_pos, per_page, content_types_query);

  if (!responce.has_value()) {
    XELOGW(fmt::format("{} - Page: {} Failed!", __func__, page_pos));
    return std::nullopt;
  }

  XELOGD(fmt::format("{} - Page: {} Loaded!", __func__,
                     responce->GetCurrentPage()));

  return responce;
}

std::future<TitleGamerpicBrowser::PageResult>
TitleGamerpicBrowser::LoadNextPage() {
  return LoadPageAsync(current_page_.GetNextPage());
}

std::future<TitleGamerpicBrowser::PageResult>
TitleGamerpicBrowser::LoadPreviousPage() {
  return LoadPageAsync(current_page_.GetPreviousPage());
}

std::future<TitleGamerpicBrowser::PageResult>
TitleGamerpicBrowser::LoadFirstPage() {
  return LoadPageAsync(1);
}

std::future<TitleGamerpicBrowser::PageResult>
TitleGamerpicBrowser::LoadLastPage() {
  return LoadPageAsync(current_page_.GetTotalPages());
}

void TitleGamerpicBrowser::LoadGameImagesAsync(
    xe::kernel::PageGamerpicsObjectJSON page_info) {
  std::stop_source thread_source = {};

  load_title_images_worker_threads_.push_back(thread_source);

  std::jthread thread(
      std::bind_front(&TitleGamerpicBrowser::LoadGameImages, this),
      thread_source.get_token(), thread_source, page_info, title_images_,
      immediate_title_images_,
      std::move(emulator_window_->imgui_drawer_shared()));

  thread.detach();
}

void TitleGamerpicBrowser::LoadGameImages(
    std::stop_token stoken, std::stop_source ssource,
    xe::kernel::PageGamerpicsObjectJSON page_info,
    std::shared_ptr<AtomicTitlesMap> title_images,
    std::shared_ptr<AtomicImmediateTitlesMap> immediate_title_images,
    std::shared_ptr<ui::ImGuiDrawer> imgui_drawer) {
  std::unordered_map<uint32_t, std::string> game_images_desc = {};

  const auto images = title_images->load(std::memory_order_acquire);

  for (const auto& title : page_info.GetTitles()) {
    // If title is already cached then skip
    if (images && images->contains(title.id)) {
      continue;
    }

    game_images_desc[title.id] = title.image;
  }

  const auto images_to_process =
      kernel_state_->GetXboxLiveAPI()->GetMultiGameInfo(game_images_desc);

  TitlesMap downloaded_images = {};

  for (const auto& title : page_info.GetTitles()) {
    if (images_to_process.contains(title.id)) {
      const bool is_png = IsDataPngImage(images_to_process.at(title.id));
      const bool is_jpeg = IsDataJpegImage(images_to_process.at(title.id));

      if (is_png || is_jpeg) {
        downloaded_images[title.id] = images_to_process.at(title.id);
      } else {
        XELOGW(fmt::format("{} - Invalid PNG/JPG: {}", __func__, title.name));
      }
    }
  }

  // We want to cache for performance
  UpdateAtomicMap(*title_images, [&](TitlesMap& new_images) {
    for (const auto& [title_id, image] : downloaded_images) {
      new_images.try_emplace(title_id, image);
    }
  });

  const auto cached_images = title_images->load(std::memory_order_acquire);

  // Processing images asynchronously prevents ImGui drawing from being
  // blocked.
  const auto immediate_images =
      immediate_title_images->load(std::memory_order_acquire);

  ImmediateTitleMap created_images = {};

  for (const auto& title : page_info.GetTitles()) {
    if (stoken.stop_requested()) {
      XELOGD(fmt::format("{} - {}: Stop Requested!", __func__, title.name));
      return;
    }

    // If immediate image is already created then skip
    if (immediate_images && immediate_images->contains(title.id)) {
      continue;
    }

    if (cached_images && cached_images->contains(title.id)) {
      XELOGD(fmt::format("{} - Loading Icon: {}", __func__, title.name));

      created_images[title.id] =
          imgui_drawer->LoadImGuiIcon(cached_images->at(title.id));

      // Animate loading images
      // std::this_thread::sleep_for(25ms);
    }
  }

  // Submit all images at the same time.
  UpdateAtomicMap(*immediate_title_images,
                  [&](ImmediateTitleMap& new_immediate_images) {
                    for (const auto& [title_id, icon] : created_images) {
                      new_immediate_images.try_emplace(title_id, icon);
                    }
                  });

  // Self-request stop so we know thread is completed to cleanup stop_source
  // vector.
  ssource.request_stop();
}

void TitleGamerpicBrowser::LoadDashboardGamerpicsAsync(
    xe::kernel::GameTitle game) {
  std::jthread thread(
      std::bind_front(&TitleGamerpicBrowser::LoadGamerpics, this), game,
      title_gamerpics_, immediate_title_gamerpics_,
      std::move(emulator_window_->imgui_drawer_shared()));

  load_dashboard_gamerpics_worker_thread_ = thread.get_stop_source();

  thread.detach();
}

void TitleGamerpicBrowser::LoadGamerpicsAsync(xe::kernel::GameTitle game) {
  std::jthread thread(
      std::bind_front(&TitleGamerpicBrowser::LoadGamerpics, this), game,
      title_gamerpics_, immediate_title_gamerpics_,
      std::move(emulator_window_->imgui_drawer_shared()));

  load_gamerpics_worker_thread_ = thread.get_stop_source();

  thread.detach();
}

void TitleGamerpicBrowser::LoadGamerpics(
    std::stop_token stoken, xe::kernel::GameTitle game,
    std::shared_ptr<AtomicGamerpicsMap> title_gamerpics,
    std::shared_ptr<AtomicImmediateTitleGamerpics> immediate_title_gamerpics,
    std::shared_ptr<ui::ImGuiDrawer> imgui_drawer) {
  auto gamerpics = title_gamerpics->load(std::memory_order_acquire);

  auto begin = game.gamerpics.cbegin();
  auto end = game.gamerpics.cend();

  const uint32_t range = 150;

  // Open a connection for every 150 gamerpics or less
  while (begin != end && !stoken.stop_requested()) {
    auto next_end =
        std::next(begin, std::min<size_t>(range, std::distance(begin, end)));

    std::vector<std::string> gamerpic_urls = {};

    for (const auto& gamerpic : std::ranges::subrange(begin, next_end)) {
      if (gamerpics && gamerpics->contains(game.id)) {
        if (gamerpics->at(game.id).contains(gamerpic.big_tile_id)) {
          continue;
        }
      }

      gamerpic_urls.push_back(gamerpic.url);
    }

    const auto gamerpics_to_process =
        kernel_state_->GetXboxLiveAPI()->GetMultiGamerpics(gamerpic_urls);

    TitlesMap downloaded_gamerpics = {};

    for (const auto& gamerpic : std::ranges::subrange(begin, next_end)) {
      if (gamerpics_to_process.contains(gamerpic.big_tile_id)) {
        const bool is_png =
            IsDataPngImage((gamerpics_to_process.at(gamerpic.big_tile_id)));
        const bool is_jpeg =
            IsDataJpegImage(gamerpics_to_process.at(gamerpic.big_tile_id));

        if (is_png || is_jpeg) {
          downloaded_gamerpics[gamerpic.big_tile_id] =
              gamerpics_to_process.at(gamerpic.big_tile_id);
        } else {
          XELOGW(fmt::format("{} - Invalid Gamerpic PNG/JPEG: {:08X}", __func__,
                             gamerpic.big_tile_id));
        }
      }
    }

    // We want to cache for performance
    UpdateAtomicMap(*title_gamerpics, [&](GamerpicsMap& new_gamerpics) {
      auto& new_title_gamerpics = new_gamerpics[game.id];

      for (const auto& [tile_id, gamerpic] : downloaded_gamerpics) {
        new_title_gamerpics.try_emplace(tile_id, gamerpic);
      }
    });

    gamerpics = title_gamerpics->load(std::memory_order_acquire);

    // Processing images asynchronously prevents ImGui drawing from being
    // blocked.
    for (const auto& gamerpic : std::ranges::subrange(begin, next_end)) {
      const auto immediate_gamerpics =
          immediate_title_gamerpics->load(std::memory_order_acquire);

      if (stoken.stop_requested()) {
        XELOGD(fmt::format("{} - {}: Stop Requested!", __func__, game.name));
        return;
      }

      // If immediate image is already created then skip
      if (immediate_gamerpics && immediate_gamerpics->contains(game.id)) {
        if (immediate_gamerpics->at(game.id).contains(gamerpic.big_tile_id)) {
          continue;
        }
      }

      if (gamerpics && gamerpics->contains(game.id)) {
        if (gamerpics->at(game.id).contains(gamerpic.big_tile_id)) {
          XELOGD(fmt::format("{} - {}: Loading Gamerpic {:08X}", __func__,
                             game.name, gamerpic.big_tile_id));

          std::shared_ptr<xe::ui::ImmediateTexture> icon =
              imgui_drawer->LoadImGuiIcon(
                  gamerpics->at(game.id).at(gamerpic.big_tile_id));

          // Submit each gamerpic individually.
          UpdateAtomicMap(*immediate_title_gamerpics,
                          [&](ImmediateGamerpicsMap& new_immediate_gamerpics) {
                            new_immediate_gamerpics[game.id].try_emplace(
                                gamerpic.big_tile_id, icon);
                          });

          // Animate loading gamerpics
          // std::this_thread::sleep_for(25ms);
        }
      }
    }

    begin = next_end;
  }

  if (stoken.stop_requested()) {
    XELOGD(fmt::format("{} - {}: Stop Requested!", __func__, game.name));
  } else {
    XELOGD(fmt::format("{} - {} Completed!", __func__, game.name));
  }
}

}  // namespace app
}  // namespace xe
