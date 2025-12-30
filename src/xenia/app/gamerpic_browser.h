/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_APP_GAMERPIC_BORWSER_DIALOG_H_
#define XENIA_APP_GAMERPIC_BORWSER_DIALOG_H_

#include <future>
#include <vector>

#include "xenia/kernel/json/page_gamerpics_object_json.h"
#include "xenia/kernel/json/title_gamerpics_object_json.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/immediate_drawer.h"

namespace xe {
namespace app {

class EmulatorWindow;  // Forward declaration due to circular dependency

class TitleGamerpicBrowser final : public ui::ImGuiDialog {
 public:
  TitleGamerpicBrowser(ui::ImGuiDrawer* imgui_drawer,
                       EmulatorWindow* emulator_window)
      : ui::ImGuiDialog(imgui_drawer),
        emulator_window_(emulator_window),
        kernel_state_(nullptr),
        profile_(nullptr),
        profile_icon_(nullptr) {}

  static std::unique_ptr<TitleGamerpicBrowser> Create(
      ui::ImGuiDrawer* imgui_drawer, EmulatorWindow* emulator_window) {
    auto browser_ptr =
        std::make_unique<TitleGamerpicBrowser>(imgui_drawer, emulator_window);

    browser_ptr->Initalize();

    return browser_ptr;
  }

 protected:
  void OnDraw(ImGuiIO& io) override;

  void OnClose() override;

 private:
  void CleanupTitleImagesThreads();
  void CloseTitleImagesThreads();
  void CloseGamerpicsThreads();

  template <typename T>
  bool IsFutureReady(std::future<T>& t) {
    return t.valid() && t.wait_for(0ms) == std::future_status::ready;
  }

  template <typename T>
  bool IsFutureReady(std::shared_future<T>& t) {
    return t.valid() && t.wait_for(0ms) == std::future_status::ready;
  }

  struct TitleGamerpicBrowserArgs {
    bool first_draw;
    bool browser_open;
  };

  struct GamerpicBrowserArgs {
    bool first_draw;
    bool browser_open;
    std::string title_desc;
  };

  enum class TitleIDInputState { NOT_FOUND, FOUND, EMPTY };

  using TitlesMap = std::map<uint32_t, std::vector<uint8_t>>;
  using ImmediateTitleMap =
      std::map<uint32_t, std::shared_ptr<xe::ui::ImmediateTexture>>;
  using GamerpicsMap = std::map<uint32_t, TitlesMap>;
  using ImmediateGamerpicsMap = std::map<uint32_t, ImmediateTitleMap>;

  using AtomicTitlesMap = std::atomic<std::shared_ptr<const TitlesMap>>;
  using AtomicImmediateTitlesMap =
      std::atomic<std::shared_ptr<const ImmediateTitleMap>>;
  using AtomicGamerpicsMap = std::atomic<std::shared_ptr<const GamerpicsMap>>;
  using AtomicImmediateTitleGamerpics =
      std::atomic<std::shared_ptr<const ImmediateGamerpicsMap>>;

  void Initalize();

  void DrawPageSelection(xe::kernel::PageGamerpicsObjectJSON page,
                         std::string title);

  void DrawGamerpicsBrowser(const xe::kernel::GameTitle game,
                            GamerpicBrowserArgs& args);

  TitleIDInputState IsSupportedTitleID(std::span<const char> data);

  void DrawInputTextBoxWithHint(
      std::string label, std::string hint, char* buffer, size_t buffer_size,
      std::function<TitleIDInputState(std::span<char>)> on_input_change);

  bool IsValidHexString(std::string hex_string);

  void LoadGamerpic();

  std::optional<kernel::xam::UserSetting> GetGamerPictureKey();

  bool IsCurrentGamerpic(xe::kernel::GameTitle game,
                         xe::kernel::Gamerpic gamerpic);

  void UpdateGamerpicIfRequested(xe::kernel::GameTitle game);

  std::set<uint32_t> GetSupportedTitlesResult(
      std::shared_future<std::set<uint32_t>>& supported_title_future,
      uint32_t retry_count);

  xe::kernel::GameTitle GetDashboardTitleResult(
      std::shared_future<xe::kernel::TitleGamerpicsObjectJSON>&
          dashboard_title_future,
      uint32_t retry_count);

  std::future<std::set<uint32_t>> GetSupportedTitlesAsync();

  std::future<xe::kernel::TitleGamerpicsObjectJSON> LoadTitleAsync(
      uint32_t title_id);

  std::future<xe::kernel::PageGamerpicsObjectJSON> LoadPageAsync(
      uint32_t page_pos);

  xe::kernel::PageGamerpicsObjectJSON LoadPage(uint32_t page_pos);

  void LoadGameImagesAsync(const xe::kernel::PageGamerpicsObjectJSON page_info);

  void LoadGameImages(
      std::stop_token stoken, std::stop_source ssource,
      const xe::kernel::PageGamerpicsObjectJSON page_info,
      std::shared_ptr<AtomicTitlesMap> title_images,
      std::shared_ptr<AtomicImmediateTitlesMap> immediate_title_images,
      std::shared_ptr<ui::ImGuiDrawer> imgui_drawer);

  void LoadDashboardGamerpicsAsync(xe::kernel::GameTitle game);

  std::future<xe::kernel::PageGamerpicsObjectJSON> LoadNextPage();

  std::future<xe::kernel::PageGamerpicsObjectJSON> LoadPreviousPage();

  std::future<xe::kernel::PageGamerpicsObjectJSON> LoadFirstPage();

  std::future<xe::kernel::PageGamerpicsObjectJSON> LoadLastPage();

  void LoadGamerpicsAsync(const xe::kernel::GameTitle game);

  void LoadGamerpics(
      std::stop_token stoken, xe::kernel::GameTitle game,
      std::shared_ptr<AtomicGamerpicsMap> title_gamerpics,
      std::shared_ptr<AtomicImmediateTitleGamerpics> immediate_title_gamerpics,
      std::shared_ptr<ui::ImGuiDrawer> imgui_drawer);

  xe::kernel::xam::UserProfile* profile_;
  EmulatorWindow* emulator_window_;
  const xe::kernel::KernelState* kernel_state_;
  xe::ui::ImmediateTexture* profile_icon_;
  std::optional<kernel::xam::UserSetting> picture_key_setting_;

  TitleGamerpicBrowserArgs titles_args_ = {};
  GamerpicBrowserArgs gamerpic_args_ = {};

  const uint32_t max_retry_count_ = 5;

  std::atomic<uint32_t> dashboard_title_retry_count_ = 0;
  bool downloaded_dash_gamerpics = false;
  std::shared_future<xe::kernel::TitleGamerpicsObjectJSON> dashboard_title_;

  std::atomic<uint32_t> supported_titles_retry_count_ = 0;
  std::shared_future<std::set<uint32_t>> supported_titles_result_ = {};

  std::future<std::vector<uint8_t>> small_gamerpic_;
  bool update_gamerpic_ = false;
  xe::kernel::Gamerpic new_gamerpic_ = {};

  xe::kernel::GameTitle selected_title_gamerpics_ = {};

  xe::kernel::PageGamerpicsObjectJSON current_page_;
  std::shared_future<xe::kernel::PageGamerpicsObjectJSON> gamerpic_page_;
  std::atomic<bool> loaded_page_ = false;

  std::shared_ptr<AtomicTitlesMap> title_images_;
  std::shared_ptr<AtomicImmediateTitlesMap> immediate_title_images_;

  std::shared_ptr<AtomicGamerpicsMap> title_gamerpics_;
  std::shared_ptr<AtomicImmediateTitleGamerpics> immediate_title_gamerpics_;

  std::vector<std::stop_source> load_title_images_worker_threads_;
  std::optional<std::stop_source> load_gamerpics_worker_thread_;
  std::optional<std::stop_source> load_dashboard_gamerpics_worker_thread_;

  bool page_changed_ = true;

  int selected_profile_ = 0;
  std::vector<uint64_t> xuids_;
  std::vector<std::string> profiles_ = {};

  std::vector<const char*> per_page_options_ = {"20", "30", "40", "50"};
  int selected_per_page_ = 0;
  std::atomic<uint32_t> per_page_ = 20;

  bool title_type_filter_state_[5] = {true, false, false, false, false};
  std::vector<const char*> title_type_filter_ = {
      "Games", "Promotional", "Magazines", "Bonus", "Unknown",
  };
  bool title_type_filter_changed_ = false;

  char title_id_[9] = {};

  uint32_t columns_per_page_ = 5;
  uint32_t title_gamerpics_columns_ = 5;

  bool page_selection_open_ = false;
  int selected_page_step_ = 1;
  uint32_t selected_page_pos_ = 1;
};

}  // namespace app
}  // namespace xe

#endif  // XENIA_APP_GAMERPIC_BORWSER_DIALOG_H_
