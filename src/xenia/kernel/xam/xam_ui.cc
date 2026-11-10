/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/kernel/xam/xam_ui.h"
#include "xenia/app/emulator_window.h"
#include "xenia/base/png_utils.h"
#include "xenia/base/system.h"
#include "xenia/hid/input_system.h"
#include "xenia/kernel/XLiveAPI.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/user_module.h"
#include "xenia/kernel/util/friends_util.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_content_device.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/ui/file_picker.h"
#include "xenia/ui/imgui_dialog.h"
#include "xenia/ui/imgui_drawer.h"
#include "xenia/ui/imgui_guest_notification.h"
#include "xenia/ui/imgui_host_notification.h"

#include "xenia/kernel/xam/ui/community_sessions_ui.h"
#include "xenia/kernel/xam/ui/create_profile_ui.h"
#include "xenia/kernel/xam/ui/friends_ui.h"
#include "xenia/kernel/xam/ui/friends_ui_util.h"
#include "xenia/kernel/xam/ui/game_achievements_ui.h"
#include "xenia/kernel/xam/ui/gamercard_from_xuid_ui.h"
#include "xenia/kernel/xam/ui/gamercard_ui.h"
#include "xenia/kernel/xam/ui/passcode_ui.h"
#include "xenia/kernel/xam/ui/signin_ui.h"
#include "xenia/kernel/xam/ui/title_info_ui.h"

DEFINE_bool(storage_selection_dialog, false,
            "Show storage device selection dialog when the game requests it.",
            "UI");

DECLARE_int32(license_mask);

DECLARE_int32(network_mode);

DECLARE_bool(upnp);

constexpr std::chrono::milliseconds kUIDelayMillis(200);

namespace xe {
namespace kernel {
namespace xam {
// TODO(gibbed): This is all one giant WIP that seems to work better than the
// previous immediate synchronous completion of dialogs.
//
// The deferred execution of dialog handling is done in such a way that there is
// a pre-, peri- (completion), and post- callback steps.
//
// pre();
// result = completion();
// CompleteOverlapped(result);
// post();
//
// There are games that are batshit insane enough to wait for the X_OVERLAPPED
// to be completed (ie not X_ERROR_PENDING) before creating a listener to
// receive a notification, which is why we have distinct pre- and post- steps.
//
// We deliberately delay the XN_SYS_UI = false notification to give games time
// to create a listener (if they're insane enough do this).
template <typename T>
X_RESULT xeXamDispatchDialog(T* dialog,
                             std::function<X_RESULT(T*)> close_callback,
                             uint32_t overlapped) {
  auto pre = []() {
    kernel_state()->BroadcastNotification(kXNotificationSystemUI, true);
  };
  auto run = [dialog, close_callback]() -> X_RESULT {
    X_RESULT result;
    dialog->set_close_callback([&dialog, &result, &close_callback]() {
      result = close_callback(dialog);
    });
    xe::threading::Fence fence;
    xe::ui::WindowedAppContext& app_context =
        kernel_state()->emulator()->display_window()->app_context();
    if (app_context.CallInUIThreadSynchronous(
            [&dialog, &fence]() { dialog->Then(&fence); })) {
      fence.Wait();
      kernel_state()->xam_state()->is_xam_dialog_present_.store(false);
    } else {
      delete dialog;
    }
    // dialog should be deleted at this point!
    return result;
  };
  auto post = []() {
    std::jthread t([] {
      xe::threading::Sleep(kUIDelayMillis);
      kernel_state()->BroadcastNotification(kXNotificationSystemUI, false);
    });
    t.detach();
  };
  if (!overlapped) {
    pre();
    auto result = run();
    post();
    return result;
  } else {
    kernel_state()->CompleteOverlappedDeferred(run, overlapped, pre, post);
    return X_ERROR_IO_PENDING;
  }
}

template <typename T>
X_RESULT xeXamDispatchDialogEx(
    T* dialog, std::function<X_RESULT(T*, uint32_t&, uint32_t&)> close_callback,
    uint32_t overlapped) {
  auto pre = []() {
    kernel_state()->BroadcastNotification(kXNotificationSystemUI, true);
  };
  auto run = [dialog, close_callback](uint32_t& extended_error,
                                      uint32_t& length) -> X_RESULT {
    auto display_window = kernel_state()->emulator()->display_window();
    X_RESULT result;
    dialog->set_close_callback(
        [&dialog, &result, &extended_error, &length, &close_callback]() {
          result = close_callback(dialog, extended_error, length);
        });
    xe::threading::Fence fence;
    if (display_window->app_context().CallInUIThreadSynchronous(
            [&dialog, &fence]() { dialog->Then(&fence); })) {
      fence.Wait();
      kernel_state()->xam_state()->is_xam_dialog_present_.store(false);
    } else {
      delete dialog;
    }
    // dialog should be deleted at this point!
    return result;
  };
  auto post = []() {
    xe::threading::Sleep(kUIDelayMillis);
    kernel_state()->BroadcastNotification(kXNotificationSystemUI, false);
  };
  if (!overlapped) {
    pre();
    uint32_t extended_error, length;
    auto result = run(extended_error, length);
    post();
    // TODO(gibbed): do something with extended_error/length?
    return result;
  } else {
    kernel_state()->CompleteOverlappedDeferredEx(run, overlapped, pre, post);
    return X_ERROR_IO_PENDING;
  }
}

X_RESULT xeXamDispatchHeadless(std::function<X_RESULT()> run_callback,
                               uint32_t overlapped) {
  auto pre = []() {
    kernel_state()->BroadcastNotification(kXNotificationSystemUI, true);
    xe::threading::Sleep(std::chrono::milliseconds(25));
  };
  auto post = []() {
    std::jthread t([]() {
      xe::threading::Sleep(kUIDelayMillis);
      kernel_state()->BroadcastNotification(kXNotificationSystemUI, false);
    });

    t.detach();
  };
  if (!overlapped) {
    pre();
    auto result = run_callback();
    post();
    return result;
  } else {
    kernel_state()->CompleteOverlappedDeferred(run_callback, overlapped, pre,
                                               post);
    return X_ERROR_IO_PENDING;
  }
}

X_RESULT xeXamDispatchHeadlessEx(
    std::function<X_RESULT(uint32_t&, uint32_t&)> run_callback,
    uint32_t overlapped) {
  auto pre = []() {
    kernel_state()->BroadcastNotification(kXNotificationSystemUI, true);
  };
  auto post = []() {
    xe::threading::Sleep(kUIDelayMillis);
    kernel_state()->BroadcastNotification(kXNotificationSystemUI, false);
  };
  if (!overlapped) {
    pre();
    uint32_t extended_error, length;
    auto result = run_callback(extended_error, length);
    post();
    // TODO(gibbed): do something with extended_error/length?
    return result;
  } else {
    kernel_state()->CompleteOverlappedDeferredEx(run_callback, overlapped, pre,
                                                 post);
    return X_ERROR_IO_PENDING;
  }
}

template <typename T>
X_RESULT xeXamDispatchDialogAsync(T* dialog,
                                  std::function<void(T*)> close_callback) {
  kernel_state()->BroadcastNotification(kXNotificationSystemUI, true);
  // Important to pass captured vars by value here since we return from this
  // without waiting for the dialog to close so the original local vars will be
  // destroyed.
  dialog->set_close_callback([dialog, close_callback]() {
    close_callback(dialog);

    kernel_state()->xam_state()->is_xam_dialog_present_.store(false);

    auto run = []() -> void {
      xe::threading::Sleep(kUIDelayMillis);
      kernel_state()->BroadcastNotification(kXNotificationSystemUI, false);
    };

    std::thread thread(run);
    thread.detach();
  });

  return X_ERROR_SUCCESS;
}

X_RESULT xeXamDispatchHeadlessAsync(std::function<void()> run_callback) {
  kernel_state()->BroadcastNotification(kXNotificationSystemUI, true);

  auto display_window = kernel_state()->emulator()->display_window();
  display_window->app_context().CallInUIThread([run_callback]() {
    run_callback();

    kernel_state()->xam_state()->is_xam_dialog_present_.store(false);

    auto run = []() -> void {
      xe::threading::Sleep(kUIDelayMillis);
      kernel_state()->BroadcastNotification(kXNotificationSystemUI, false);
    };

    std::thread thread(run);
    thread.detach();
  });

  return X_ERROR_SUCCESS;
}

void MessageBoxDialog::OnDraw(ImGuiIO& io) {
  bool first_draw = false;
  if (!has_opened_) {
    ImGui::OpenPopup(title_.c_str());
    has_opened_ = true;
    first_draw = true;
  }
  if (ImGui::BeginPopupModal(title_.c_str(), nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    if (description_.size()) {
      ImGui::Text("%s", description_.c_str());
    }
    if (first_draw) {
      ImGui::SetKeyboardFocusHere();
    }
    for (size_t i = 0; i < buttons_.size(); ++i) {
      if (ImGui::Button(buttons_[i].c_str())) {
        chosen_button_ = static_cast<uint32_t>(i);
        ImGui::CloseCurrentPopup();
        Close();
      }
      ImGui::SameLine();
    }
    ImGui::Spacing();
    ImGui::Spacing();
    ImGui::EndPopup();
  } else {
    Close();
  }
}

void KeyboardInputDialog::OnDraw(ImGuiIO& io) {
  bool first_draw = false;
  if (!has_opened_) {
    ImGui::OpenPopup(title_.c_str());
    has_opened_ = true;
    first_draw = true;
  }
  if (ImGui::BeginPopupModal(title_.c_str(), nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    if (description_.size()) {
      ImGui::TextWrapped("%s", description_.c_str());
    }
    if (first_draw) {
      ImGui::SetKeyboardFocusHere();
    }
    ImGui::PushID("input_text");
    bool input_submitted =
        ImGui::InputText("##body", text_buffer_.data(), text_buffer_.size(),
                         ImGuiInputTextFlags_EnterReturnsTrue);
    // Context menu for paste functionality
    if (ImGui::BeginPopupContextItem("input_context_menu")) {
      if (ImGui::MenuItem("Paste")) {
        if (ImGui::GetClipboardText() != nullptr) {
          std::string clipboard_text = ImGui::GetClipboardText();
          xe::string_util::copy_truncating(text_buffer_.data(), clipboard_text,
                                           text_buffer_.size());
        }
      }
      ImGui::EndPopup();
    }
    ImGui::PopID();
    if (input_submitted) {
      text_ = std::string(text_buffer_.data(), text_buffer_.size());
      cancelled_ = false;
      ImGui::CloseCurrentPopup();
      Close();
    }
    if (ImGui::Button("OK")) {
      text_ = std::string(text_buffer_.data(), text_buffer_.size());
      cancelled_ = false;
      ImGui::CloseCurrentPopup();
      Close();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      text_ = "";
      cancelled_ = true;
      ImGui::CloseCurrentPopup();
      Close();
    }
    ImGui::Spacing();
    ImGui::EndPopup();
  } else {
    Close();
  }
}

static dword_result_t XamShowMessageBoxUi(
    dword_t user_index, lpu16string_t title_ptr, lpu16string_t text_ptr,
    dword_t button_count, lpdword_t button_ptrs, dword_t active_button,
    dword_t flags, pointer_t<MESSAGEBOX_RESULT> result_ptr,
    pointer_t<XAM_OVERLAPPED> overlapped) {
  std::string title = title_ptr ? xe::to_utf8(title_ptr.value()) : "";
  std::string text = text_ptr ? xe::to_utf8(text_ptr.value()) : "";

  std::vector<std::string> buttons;
  for (uint32_t i = 0; i < button_count; ++i) {
    uint32_t button_ptr = button_ptrs[i];
    auto button = xe::load_and_swap<std::u16string>(
        kernel_state()->memory()->TranslateVirtual(button_ptr));

    if (!button.empty()) {
      buttons.push_back(xe::to_utf8(button));
    }
  }

  if (buttons.empty()) {
    buttons.push_back("OK");
  }

  X_RESULT result;
  if (cvars::headless) {
    // Auto-pick the focused button.
    auto run = [result_ptr, active_button]() -> X_RESULT {
      result_ptr->ButtonPressed = static_cast<uint32_t>(active_button);
      return X_ERROR_SUCCESS;
    };

    result = xeXamDispatchHeadless(run, overlapped);
  } else {
    switch (flags & 0xF) {
      case XMBox_NOICON: {
      } break;
      case XMBox_ERRORICON: {
      } break;
      case XMBox_WARNINGICON: {
      } break;
      case XMBox_ALERTICON: {
      } break;
    }

    if (kernel_state()->xam_state()->IsUIActive()) {
      return X_ERROR_ACCESS_DENIED;
    }

    kernel_state()->xam_state()->is_xam_dialog_present_.store(true);

    const Emulator* emulator = kernel_state()->emulator();
    xe::ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();

    if (flags & XMBox_PASSCODEMODE || flags & XMBox_VERIFYPASSCODEMODE) {
      auto close = [result_ptr,
                    active_button](ui::ProfilePasscodeUI* dialog) -> X_RESULT {
        if (dialog->SelectedSignedIn()) {
          // Logged in
          return X_ERROR_SUCCESS;
        } else {
          return X_ERROR_FUNCTION_FAILED;
        }
      };

      result = xeXamDispatchDialog<ui::ProfilePasscodeUI>(
          new ui::ProfilePasscodeUI(imgui_drawer, title, text, result_ptr),
          close, overlapped);
    } else {
      auto close = [result_ptr](MessageBoxDialog* dialog) -> X_RESULT {
        result_ptr->ButtonPressed = dialog->chosen_button();
        kernel_state()->xam_state()->is_xam_dialog_present_.store(false);
        return X_ERROR_SUCCESS;
      };

      result = xeXamDispatchDialog<MessageBoxDialog>(
          new MessageBoxDialog(imgui_drawer, title, text, buttons,
                               static_cast<uint32_t>(active_button)),
          close, overlapped);
    }
  }

  return result;
}

dword_result_t XamIsUIActive_entry() {
  return kernel_state()->xam_state()->IsUIActive();
}
DECLARE_XAM_EXPORT2(XamIsUIActive, kUI, kImplemented, kHighFrequency);

// https://www.se7ensins.com/forums/threads/working-xshowmessageboxui.844116/
dword_result_t XamShowMessageBoxUI_entry(
    dword_t user_index, lpu16string_t title_ptr, lpu16string_t text_ptr,
    dword_t button_count, lpdword_t button_ptrs, dword_t active_button,
    dword_t flags, pointer_t<MESSAGEBOX_RESULT> result_ptr,
    pointer_t<XAM_OVERLAPPED> overlapped) {
  return XamShowMessageBoxUi(user_index, title_ptr, text_ptr, button_count,
                             button_ptrs, active_button, flags, result_ptr,
                             overlapped);
}
DECLARE_XAM_EXPORT1(XamShowMessageBoxUI, kUI, kImplemented);

dword_result_t XamShowMessageBoxUIEx_entry(
    dword_t user_index, lpu16string_t title_ptr, lpu16string_t text_ptr,
    dword_t button_count, lpdword_t button_ptrs, dword_t active_button,
    dword_t flags, dword_t unknown_unused,
    pointer_t<MESSAGEBOX_RESULT> result_ptr,
    pointer_t<XAM_OVERLAPPED> overlapped) {
  return XamShowMessageBoxUi(user_index, title_ptr, text_ptr, button_count,
                             button_ptrs, active_button, flags, result_ptr,
                             overlapped);
}
DECLARE_XAM_EXPORT1(XamShowMessageBoxUIEx, kUI, kImplemented);

// XamShowMessageBoxUI with the tracking ID of a Kinect skeleton first.
// TODO(knuckleslee): Selecting the buttons with Kinect is not supported.
dword_result_t XamShowNuiMessageBoxUI_entry(
    dword_t tracking_id, dword_t user_index, lpu16string_t title_ptr,
    lpu16string_t text_ptr, dword_t button_count, lpdword_t button_ptrs,
    dword_t active_button, dword_t flags,
    pointer_t<MESSAGEBOX_RESULT> result_ptr,
    pointer_t<XAM_OVERLAPPED> overlapped) {
  auto result = kernel_state()->nui()->NuiHudCheck(tracking_id);
  if (result) {
    return result;
  }
  return XamShowMessageBoxUi(user_index, title_ptr, text_ptr, button_count,
                             button_ptrs, active_button, flags | 0x2000,
                             result_ptr, overlapped);
}
DECLARE_XAM_EXPORT1(XamShowNuiMessageBoxUI, kUI, kSketchy);

dword_result_t XNotifyQueueUI_entry(dword_t exnq, dword_t dwUserIndex,
                                    qword_t qwAreas,
                                    lpu16string_t displayText_ptr,
                                    lpvoid_t contextData) {
  std::string displayText = "";
  const uint8_t position_id = static_cast<uint8_t>(qwAreas);

  if (displayText_ptr) {
    displayText = xe::to_utf8(displayText_ptr.value());
  }

  XELOGI("XNotifyQueueUI: {}", displayText);

  const Emulator* emulator = kernel_state()->emulator();
  xe::ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();

  new xe::ui::XNotifyWindow(imgui_drawer, "", displayText, dwUserIndex,
                            position_id);

  // XNotifyQueueUI -> XNotifyQueueUIEx -> XMsgProcessRequest ->
  // XMsgStartIORequestEx & XMsgInProcessCall
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XNotifyQueueUI, kUI, kSketchy);

// https://www.se7ensins.com/forums/threads/release-how-to-use-xshowkeyboardui-release.906568/
dword_result_t XamShowKeyboardUI_entry(
    dword_t user_index, dword_t flags, lpu16string_t default_text,
    lpu16string_t title, lpu16string_t description, lpu16string_t buffer,
    dword_t buffer_length, pointer_t<XAM_OVERLAPPED> overlapped) {
  if (!buffer) {
    return X_ERROR_INVALID_PARAMETER;
  }

  assert_not_null(overlapped);

  auto buffer_size = static_cast<size_t>(buffer_length) * sizeof(char16_t);

  X_RESULT result;
  if (cvars::headless) {
    auto run = [default_text, buffer, buffer_length,
                buffer_size]() -> X_RESULT {
      // Redirect default_text back into the buffer.
      if (!default_text) {
        std::memset(buffer, 0, buffer_size);
      } else {
        string_util::copy_and_swap_truncating(buffer, default_text.value(),
                                              buffer_length);
      }
      return X_ERROR_SUCCESS;
    };
    result = xeXamDispatchHeadless(run, overlapped);
  } else {
    auto close = [buffer, buffer_length](KeyboardInputDialog* dialog,
                                         uint32_t& extended_error,
                                         uint32_t& length) -> X_RESULT {
      if (dialog->cancelled()) {
        extended_error = X_ERROR_CANCELLED;
        length = 0;
        return X_ERROR_SUCCESS;
      } else {
        // Zero the output buffer.
        auto text = xe::to_utf16(dialog->text());
        string_util::copy_and_swap_truncating(buffer, text, buffer_length);
        extended_error = X_ERROR_SUCCESS;
        length = 0;
        return X_ERROR_SUCCESS;
      }
    };

    if (kernel_state()->xam_state()->IsUIActive()) {
      return X_ERROR_ACCESS_DENIED;
    }

    kernel_state()->xam_state()->is_xam_dialog_present_.store(true);

    const Emulator* emulator = kernel_state()->emulator();
    xe::ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();

    std::string title_str = title ? xe::to_utf8(title.value()) : "";
    std::string desc_str = description ? xe::to_utf8(description.value()) : "";
    std::string def_text_str =
        default_text ? xe::to_utf8(default_text.value()) : "";

    result = xeXamDispatchDialogEx<KeyboardInputDialog>(
        new KeyboardInputDialog(imgui_drawer, title_str, desc_str, def_text_str,
                                buffer_length),
        close, overlapped);
  }
  return result;
}
DECLARE_XAM_EXPORT1(XamShowKeyboardUI, kUI, kImplemented);

dword_result_t XamShowDeviceSelectorUI_entry(
    dword_t user_index, dword_t content_type, dword_t content_flags,
    qword_t total_requested, lpdword_t device_id_ptr,
    pointer_t<XAM_OVERLAPPED> overlapped) {
  if (!overlapped) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if ((user_index >= XUserMaxUserCount && user_index != XUserIndexAny) ||
      (content_flags & 0x83F00008) != 0 || !device_id_ptr) {
    XOverlappedSetExtendedError(overlapped, X_ERROR_INVALID_PARAMETER);
    return X_ERROR_INVALID_PARAMETER;
  }

  std::vector<const DummyDeviceInfo*> devices = ListStorageDevices();

  if (cvars::headless || !cvars::storage_selection_dialog) {
    // Default to the first storage device (HDD) if headless.
    return xeXamDispatchHeadless(
        [device_id_ptr, devices]() -> X_RESULT {
          if (devices.empty()) {
            return X_ERROR_CANCELLED;
          }

          const DummyDeviceInfo* device_info = devices.front();
          *device_id_ptr = static_cast<uint32_t>(device_info->device_id);
          return X_ERROR_SUCCESS;
        },
        overlapped);
  }

  auto close = [device_id_ptr, devices](MessageBoxDialog* dialog) -> X_RESULT {
    uint32_t button = dialog->chosen_button();
    if (button >= devices.size()) {
      return X_ERROR_CANCELLED;
    }

    const DummyDeviceInfo* device_info = devices.at(button);
    *device_id_ptr = static_cast<uint32_t>(device_info->device_id);
    return X_ERROR_SUCCESS;
  };

  if (kernel_state()->xam_state()->IsUIActive()) {
    return X_ERROR_ACCESS_DENIED;
  }

  kernel_state()->xam_state()->is_xam_dialog_present_.store(true);

  std::string title = "Select storage device";
  std::string desc = "";

  cxxopts::OptionNames buttons;
  for (auto& device_info : devices) {
    buttons.push_back(to_utf8(device_info->name));
  }
  buttons.push_back("Cancel");

  const Emulator* emulator = kernel_state()->emulator();
  xe::ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();
  return xeXamDispatchDialog<MessageBoxDialog>(
      new MessageBoxDialog(imgui_drawer, title, desc, buttons, 0), close,
      overlapped);
}
DECLARE_XAM_EXPORT1(XamShowDeviceSelectorUI, kUI, kImplemented);

// XamShowDeviceSelectorUI with the tracking ID of a Kinect skeleton first.
dword_result_t XamShowNuiDeviceSelectorUI_entry(
    dword_t tracking_id, dword_t user_index, dword_t content_type,
    dword_t content_flags, qword_t total_requested, lpdword_t device_id_ptr,
    pointer_t<XAM_OVERLAPPED> overlapped) {
  auto result = kernel_state()->nui()->NuiHudCheck(tracking_id);
  if (result) {
    return result;
  }
  return XamShowDeviceSelectorUI_entry(user_index, content_type, content_flags,
                                       total_requested, device_id_ptr,
                                       overlapped);
}
DECLARE_XAM_EXPORT1(XamShowNuiDeviceSelectorUI, kUI, kSketchy);

std::string disc_title = "Disc Read Error";
std::string disc_desc =
    "There's been an issue reading content from the game disc.\nThis is "
    "likely caused by bad or unimplemented file IO calls.";

dword_result_t XamShowDirtyDiscErrorUI_entry(dword_t user_index) {
  // uses XamIsDataCenterMode & XamShowAndWaitForMessageBoxEx, same as
  // XamShowNuiDirtyDiscErrorUI except flag = 1, user index always set to
  // XUserIndexAny
  if (cvars::headless) {
    assert_always();
    return X_ERROR_SUCCESS;
  }

  const Emulator* emulator = kernel_state()->emulator();
  xe::ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();
  xeXamDispatchDialog<MessageBoxDialog>(
      new MessageBoxDialog(imgui_drawer, disc_title, disc_desc, {"OK"}, 0),
      [](MessageBoxDialog*) -> X_RESULT { return X_ERROR_SUCCESS; }, 0);
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamShowDirtyDiscErrorUI, kUI, kStub);

dword_result_t XamShowNuiDirtyDiscErrorUI_entry(dword_t tracking_id) {
  auto result = kernel_state()->nui()->NuiHudCheck(tracking_id);
  if (result) {
    return result;
  }
  // XamShowAndWaitForMessageBoxEx, same as XamShowDirtyDiscErrorUI except flag
  // = 0x2001, user index always set to XUserIndexAny
  if (cvars::headless) {
    assert_always();
    return X_ERROR_SUCCESS;
  }

  const Emulator* emulator = kernel_state()->emulator();
  xe::ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();
  xeXamDispatchDialog<MessageBoxDialog>(
      new MessageBoxDialog(imgui_drawer, disc_title, disc_desc, {"OK"}, 0),
      [](MessageBoxDialog*) -> X_RESULT { return X_ERROR_SUCCESS; }, 0);
  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamShowNuiDirtyDiscErrorUI, kUI, kStub);

dword_result_t XamShowPartyUI_entry(dword_t user_index) {
  if (!kernel_state()->xam_state()->user_tracker()->LoggedInToLive()) {
    return X_ERROR_ACCESS_DENIED;
  }

  return X_ERROR_FUNCTION_FAILED;
}
DECLARE_XAM_EXPORT1(XamShowPartyUI, kUI, kStub);

// https://gitlab.com/GlitchyScripts/xlivelessness/-/blob/master/xlivelessness/xlive/xdefs.hpp?ref_type=heads#L1235
dword_result_t XamShowMarketplaceUIEx_entry(dword_t user_index, dword_t ui_type,
                                            qword_t offer_id,
                                            dword_t offer_type,
                                            dword_t content_category,
                                            unknown_t unk6, unknown_t unk7,
                                            dword_t title_id) {
  // ui_type:
  // 0 - view all content for the current title
  // 1 - view content specified by offer id
  // offer_types:
  // filter for content list, usually just -1
  // content_category:
  // filter on item types for games (e.g. cars, maps, weapons, etc)
  if (user_index >= XUserMaxUserCount || ui_type >= MaxEntrypoint) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    return X_ERROR_NO_SUCH_USER;
  }

  if (cvars::headless) {
    return xeXamDispatchHeadlessAsync([]() {});
  }

  if (kernel_state()->xam_state()->IsUIActive()) {
    return X_ERROR_ACCESS_DENIED;
  }

  kernel_state()->xam_state()->is_xam_dialog_present_.store(true);

  bool is_xbla_unlock_offer =
      (offer_id == ((uint64_t(kernel_state()->title_id()) << 32) | 1ull));

  auto close = [ui_type,
                is_xbla_unlock_offer](MessageBoxDialog* dialog) -> void {
    if (ui_type == 1 && is_xbla_unlock_offer) {
      uint32_t button = dialog->chosen_button();
      if (button == 0) {
        cvars::license_mask = 1;

        kernel_state()->BroadcastNotification(
            kXNotificationLiveContentInstalled, 0);
      }
    }
  };

  std::string title = "Xbox Marketplace";
  std::string desc = "";
  cxxopts::OptionNames buttons;

  switch (ui_type) {
    case X_MARKETPLACE_ENTRYPOINT::ContentList:
      desc =
          "Game requested to open marketplace page with all content for the "
          "current title ID.";
      break;
    case X_MARKETPLACE_ENTRYPOINT::ContentItem:
      desc = fmt::format(
          "Game requested to open marketplace page for offer ID 0x{:016X}.",
          static_cast<uint64_t>(offer_id));
      break;
    case X_MARKETPLACE_ENTRYPOINT::MembershipList:
      desc =
          "Game requested to open marketplace page with all Xbox Live "
          "memberships.";
      break;
    case X_MARKETPLACE_ENTRYPOINT::MembershipItem:
      desc = fmt::format(
          "Game requested to open marketplace page for an Xbox Live "
          "membership offer 0x{:016X}.",
          static_cast<uint64_t>(offer_id));
      break;
    case X_MARKETPLACE_ENTRYPOINT::ContentList_Background:
      // Used when accessing microsoft points
      desc = fmt::format(
          "Xbox Marketplace requested access to Microsoft Points offer page "
          "0x{:016X}.",
          static_cast<uint64_t>(offer_id));
      break;
    case X_MARKETPLACE_ENTRYPOINT::ContentItem_Background:
      // Used when accessing credit card information and calls
      // XamShowCreditCardUI
      desc = fmt::format(
          "Xbox Marketplace requested access to credit card information page "
          "0x{:016X}.",
          static_cast<uint64_t>(offer_id));
      break;
    case X_MARKETPLACE_ENTRYPOINT::ForcedNameChangeV1:
      // Used by XamShowForcedNameChangeUI v1888
      desc = fmt::format("Changing gamertag currently not implemented.");
      break;
    case X_MARKETPLACE_ENTRYPOINT::ForcedNameChangeV2:
      // Used by XamShowForcedNameChangeUI NXE and up
      desc = fmt::format("Changing gamertag currently not implemented.");
      break;
    case X_MARKETPLACE_ENTRYPOINT::GamerTagPurchase:
      // Used by dashboard when selecting change gamertag in profile menu
      desc = fmt::format("Changing gamertag currently not implemented.");
      break;
    case X_MARKETPLACE_ENTRYPOINT::ActiveDownloads:
      // Used in profile tabs when clicking active downloads
      desc = fmt::format(
          "There are no current plans to download files from Xbox "
          "Marketplace.");
      break;
    default:
      desc = fmt::format("Unknown marketplace op {:d}",
                         static_cast<uint32_t>(ui_type));
      break;
  }

  desc +=
      "\nNote that since Xenia cannot access Xbox Marketplace, any DLC must be "
      "installed manually using File -> Install Content.";

  switch (ui_type) {
    case X_MARKETPLACE_ENTRYPOINT::ContentList:
    default:
      buttons.push_back("OK");
      break;
    case X_MARKETPLACE_ENTRYPOINT::ContentItem:
      if (is_xbla_unlock_offer) {
        desc +=
            "\n\nTo start trial games in full mode, set license_mask to 1 in "
            "Xenia config file.\n\nDo you wish to change license_mask to 1 for "
            "*this session*?";
        buttons.push_back("Yes");
        buttons.push_back("No");
      } else {
        buttons.push_back("OK");
      }
      break;
  }

  const Emulator* emulator = kernel_state()->emulator();
  xe::ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();
  return xeXamDispatchDialogAsync<MessageBoxDialog>(
      new MessageBoxDialog(imgui_drawer, title, desc, buttons, 0), close);
}
DECLARE_XAM_EXPORT1(XamShowMarketplaceUIEx, kUI, kSketchy);

dword_result_t XamShowMarketplaceUI_entry(dword_t user_index, dword_t ui_type,
                                          qword_t offer_id, dword_t offer_type,
                                          dword_t content_category,
                                          dword_t title_id) {
  return XamShowMarketplaceUIEx_entry(user_index, ui_type, offer_id, offer_type,
                                      content_category, 0, 0, title_id);
}
DECLARE_XAM_EXPORT1(XamShowMarketplaceUI, kUI, kSketchy);

dword_result_t XamShowMarketplaceDownloadItemsUI_entry(
    dword_t user_index, dword_t ui_type, lpqword_t offers, dword_t num_offers,
    lpdword_t hresult_ptr, pointer_t<XAM_OVERLAPPED> overlapped) {
  if (user_index >= XUserMaxUserCount || !offers || num_offers > 6) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!kernel_state()->xam_state()->IsUserSignedIn(user_index)) {
    if (overlapped) {
      kernel_state()->CompleteOverlappedImmediate(overlapped,
                                                  X_ERROR_NO_SUCH_USER);
      return X_ERROR_IO_PENDING;
    }
    return X_ERROR_NO_SUCH_USER;
  }

  if (cvars::headless) {
    return xeXamDispatchHeadless(
        [hresult_ptr]() -> X_RESULT {
          if (hresult_ptr) {
            *hresult_ptr = X_E_SUCCESS;
          }
          return X_ERROR_SUCCESS;
        },
        overlapped);
  }

  if (kernel_state()->xam_state()->IsUIActive()) {
    return X_ERROR_ACCESS_DENIED;
  }

  kernel_state()->xam_state()->is_xam_dialog_present_.store(true);

  auto close = [hresult_ptr](MessageBoxDialog* dialog) -> X_RESULT {
    if (hresult_ptr) {
      // TODO
      *hresult_ptr = X_E_SUCCESS;
    }
    return X_ERROR_SUCCESS;
  };

  std::string title = "Xbox Marketplace";
  std::string desc = "";
  cxxopts::OptionNames buttons = {"OK"};

  switch (ui_type) {
    case X_MARKETPLACE_DOWNLOAD_ITEMS_ENTRYPOINTS::FREEITEMS:
      desc =
          "Game requested to open download page for the following free offer "
          "IDs:";
      break;
    case X_MARKETPLACE_DOWNLOAD_ITEMS_ENTRYPOINTS::PAIDITEMS:
      desc =
          "Game requested to open download page for the following offer IDs:";
      break;
    default:
      return X_ERROR_INVALID_PARAMETER;
  }

  for (uint32_t i = 0; i < num_offers; i++) {
    desc += fmt::format("\n0x{:16X}", offers[i].get());
  }

  desc +=
      "\n\nNote that since Xenia cannot access Xbox Marketplace, any DLC "
      "must "
      "be installed manually using File -> Install Content.";

  const Emulator* emulator = kernel_state()->emulator();
  xe::ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();
  return xeXamDispatchDialog<MessageBoxDialog>(
      new MessageBoxDialog(imgui_drawer, title, desc, buttons, 0), close,
      overlapped);
}
DECLARE_XAM_EXPORT1(XamShowMarketplaceDownloadItemsUI, kUI, kSketchy);

dword_result_t XamShowGoldUpgradeUI_entry(dword_t user_index, dword_t unk,
                                          dword_t title_id) {
  return X_ERROR_FUNCTION_FAILED;
}
DECLARE_XAM_EXPORT1(XamShowGoldUpgradeUI, kUI, kStub);

dword_result_t XamShowMultiplayerUpgradeUI_entry(dword_t user_index) {
  return XamShowGoldUpgradeUI_entry(user_index, 6,
                                    kernel_state()->emulator()->title_id());
}
DECLARE_XAM_EXPORT1(XamShowMultiplayerUpgradeUI, kUI, kImplemented);

dword_result_t XamShowForcedNameChangeUI_entry(dword_t user_index) {
  return XamShowMarketplaceUIEx_entry(
      user_index, X_MARKETPLACE_ENTRYPOINT::ForcedNameChangeV2, 0, 0xffffffff,
      0, 0, 0, 0);
}
DECLARE_XAM_EXPORT1(XamShowForcedNameChangeUI, kUI, kImplemented);

bool xeDrawProfileContent(xe::ui::ImGuiDrawer* imgui_drawer,
                          const uint64_t xuid, const uint8_t user_index,
                          const X_XAMACCOUNTINFO* account,
                          const xe::ui::ImmediateTexture* profile_icon,
                          std::function<bool()> context_menu,
                          std::function<void()> on_profile_change,
                          uint64_t* selected_xuid) {
  const ImVec2 start_position = ImGui::GetCursorPos();

  ImGui::BeginGroup();
  {
    if (profile_icon) {
      ImGui::Image(reinterpret_cast<ImTextureID>(profile_icon),
                   xe::ui::default_image_icon_size);
    } else {
      if (user_index < XUserMaxUserCount) {
        const auto icon = imgui_drawer->GetNotificationIcon(user_index);
        ImGui::Image(reinterpret_cast<ImTextureID>(icon),
                     xe::ui::default_image_icon_size);
      } else {
        ImGui::Dummy(xe::ui::default_image_icon_size);
      }
    }

    ImGui::SameLine();

    ImGui::BeginGroup();
    {
      ImGui::TextUnformatted(
          fmt::format("User: {}\n", account->GetGamertagString()).c_str());
      ImGui::TextUnformatted(fmt::format("XUID: {:016X}  \n", xuid).c_str());

      const std::string live_enabled = fmt::format(
          "Xbox Live Enabled: {}", account->IsLiveEnabled() ? "True" : "False");

      ImGui::TextUnformatted(live_enabled.c_str());

      if (user_index != XUserIndexAny) {
        ImGui::TextUnformatted(
            fmt::format("Assigned to slot: {}\n", user_index + 1).c_str());
      } else {
        ImGui::TextUnformatted(fmt::format("Profile is not signed in").c_str());
      }
    }
    ImGui::EndGroup();
  }
  ImGui::EndGroup();

  if (xuid && selected_xuid) {
    const ImVec2 end_draw_position =
        ImVec2(ImGui::GetCursorPos().x - start_position.x,
               ImGui::GetCursorPos().y - start_position.y);

    ImGui::SetCursorPos(start_position);
    if (ImGui::Selectable("##Selectable", *selected_xuid == xuid,
                          ImGuiSelectableFlags_SpanAllColumns,
                          end_draw_position)) {
      *selected_xuid = xuid;
      ImGui::OpenPopup("Profile Menu");
    }

    if (context_menu) {
      return context_menu();
    }
  }

  return true;
}

bool xeDrawFriendContent(xe::ui::ImGuiDrawer* imgui_drawer,
                         UserProfile* profile,
                         std::shared_ptr<xe::ui::ImmediateTexture> icon_texture,
                         const X_ONLINE_FRIEND& presence,
                         uint64_t* selected_xuid_, uint64_t* removed_xuid_) {
  const uint64_t friend_xuid = presence.xuid.get();
  const std::string friend_xuid_str = fmt::format("{:016X}", friend_xuid);
  const uint32_t title_id = presence.title_id.get();

  const ImGuiStyle& style = ImGui::GetStyle();
  const ImVec2 icon_size = xe::ui::default_image_icon_size;
  const ui::FriendStatus status = ui::GetFriendStatus(presence.state.get());

  ImDrawList* draw_list = ImGui::GetWindowDrawList();

  const ImVec2 row_start_position = ImGui::GetCursorPos();

  {
    const xe::ui::ImmediateTexture* icon =
        icon_texture ? icon_texture.get() : imgui_drawer->GetLoadingTileIcon();
    const ImVec2 icon_min = ImGui::GetCursorScreenPos();
    const ImVec2 icon_max =
        ImVec2(icon_min.x + icon_size.x, icon_min.y + icon_size.y);

    draw_list->AddImageRounded(
        reinterpret_cast<ImTextureID>(icon), icon_min, icon_max,
        ImVec2(0.0f, 0.0f), ImVec2(1.0f, 1.0f),
        status.online ? IM_COL32_WHITE : IM_COL32(255, 255, 255, 128),
        ui::kFriendCardRounding);
    ImGui::Dummy(icon_size);
  }
  ImGui::SameLine();

  const ImVec2 info_start_position = ImGui::GetCursorPos();
  const ImVec2 info_min = ImGui::GetCursorScreenPos();
  const float info_width = ImGui::GetContentRegionAvail().x;
  const float info_text_right =
      info_start_position.x + info_width - ui::kFriendCardPadding.x;

  // Draw the text on the top channel so the card background, which is
  // submitted afterwards (its size depends on the text), renders behind it.
  ImDrawListSplitter splitter;
  splitter.Split(draw_list, 2);
  splitter.SetCurrentChannel(draw_list, 1);

  ImGui::SetCursorPos(ImVec2(info_start_position.x + ui::kFriendCardPadding.x,
                             info_start_position.y + ui::kFriendCardPadding.y));

  ImGui::BeginGroup();
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                      ImVec2(style.ItemSpacing.x, 0.0f));
  ImGui::PushTextWrapPos(info_text_right);
  {
    ImGui::TextUnformatted(presence.Gamertag);

    ImGui::SameLine();
    ImGui::SetCursorPosX(
        std::max(ImGui::GetCursorPosX(),
                 info_text_right - ImGui::CalcTextSize(status.label).x));
    ImGui::PushStyleColor(ImGuiCol_Text, status.color);
    ImGui::TextUnformatted(status.label);
    ImGui::PopStyleColor();

    ImGui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
    if (title_id) {
      if (title_id == kernel_state()->title_id()) {
        ImGui::TextWrapped("Playing %s",
                           kernel_state()->emulator()->title_name().c_str());
      } else {
        ImGui::Text("Playing title %08X", title_id);
      }
    }
    ImGui::PopStyleColor();

    const std::u16string rich_presence =
        xe::string_util::read_u16string_and_swap(
            reinterpret_cast<const char16_t*>(presence.wszRichPresence));

    if (!rich_presence.empty()) {
      std::string presence_string;
      for (const char c : xe::string_util::trim(xe::to_utf8(rich_presence))) {
        if (c == '\n') {
          presence_string += ", ";
        } else {
          presence_string += c;
        }
      }

      ImGui::TextWrapped("%s", presence_string.c_str());
    }

    ImGui::PushStyleColor(ImGuiCol_Text, style.Colors[ImGuiCol_TextDisabled]);
    ImGui::Text("XUID %s", friend_xuid_str.c_str());
    ImGui::PopStyleColor();
  }
  ImGui::PopTextWrapPos();
  ImGui::PopStyleVar();
  ImGui::EndGroup();

  const float info_height =
      std::max(ImGui::GetItemRectSize().y + ui::kFriendCardPadding.y * 2.0f,
               icon_size.y);
  const ImVec2 info_max =
      ImVec2(info_min.x + info_width, info_min.y + info_height);

  splitter.SetCurrentChannel(draw_list, 0);

  ImU32 card_color = ui::kFriendCardColor;

  if (selected_xuid_) {
    const std::string selectable_label =
        std::format("##Selectable{}", friend_xuid_str);
    const std::string context_label =
        std::format("Friend Menu##{}", friend_xuid_str);

    ImGui::SetCursorPos(info_start_position);

    // Selectable only provides interaction, the rounded card is drawn below.
    ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(0, 0, 0, 0));
    if (ImGui::Selectable(selectable_label.c_str(), false,
                          ImGuiSelectableFlags_NoAutoClosePopups,
                          ImVec2(info_width, info_height))) {
      *selected_xuid_ = friend_xuid;
    }
    ImGui::PopStyleColor(3);

    if (ImGui::IsItemActive() || ImGui::IsPopupOpen(context_label.c_str())) {
      card_color = ui::kFriendCardActiveColor;
    } else if (ImGui::IsItemHovered()) {
      card_color = ui::kFriendCardHoveredColor;
    }

    if (ImGui::BeginPopupContextItem(context_label.c_str())) {
      if (ImGui::BeginMenu("Copy")) {
        if (ImGui::MenuItem("Gamertag")) {
          ImGui::SetClipboardText(presence.Gamertag);
        }

        ImGui::Separator();

        if (ImGui::MenuItem("XUID Online")) {
          ImGui::SetClipboardText(friend_xuid_str.c_str());
        }

        ImGui::EndMenu();
      }
      ImGui::EndPopup();
    }
  }

  draw_list->AddRectFilled(info_min, info_max, card_color,
                           ui::kFriendCardRounding);

  splitter.Merge(draw_list);

  ImGui::SetCursorPos(ImVec2(
      row_start_position.x,
      info_start_position.y + info_height + ImGui::GetStyle().ItemSpacing.y));
  ImGui::Spacing();

  if (profile->GetOnlineXUID() == friend_xuid) {
    return true;
  }

  const ImVec2 half_width_btn = ui::HalfWidthButtonSize();

  const uint32_t current_title_id = kernel_state()->title_id();
  const bool same_title = title_id == current_title_id;
  const bool are_friends =
      kernel_state()->friends_manager()->IsFriend(profile->xuid(), friend_xuid);

  const std::string gamertag(presence.Gamertag);
  const std::string join_label =
      std::format("Join Session##{}", friend_xuid_str);
  const std::string remove_label = std::format("Remove##{}", friend_xuid_str);
  const std::string add_label = std::format("Add##{}", friend_xuid_str);

  ImGui::BeginDisabled(!presence.session_id.as_uint64() || !same_title);
  if (ImGui::Button(join_label.c_str(), half_width_btn)) {
    X_INVITE_INFO invite = {};

    invite.xuid_invitee = profile->GetOnlineXUID();
    invite.xuid_inviter = friend_xuid;
    invite.title_id = current_title_id;
    invite.from_game_invite = true;

    profile->SetSelfInvite(invite);

    const uint32_t user_index =
        kernel_state()->xam_state()->GetUserIndexAssignedToProfileFromXUID(
            profile->xuid());

    kernel_state()->BroadcastNotification(kXNotificationLiveInviteAccepted,
                                          user_index);
  }
  ImGui::EndDisabled();

  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    if (current_title_id == 0 || title_id == 0 || same_title) {
      ImGui::SetTooltip("Join gaming session");
    } else {
      ImGui::SetTooltip("%s is playing a different game", gamertag.c_str());
    }
  }

  ImGui::SameLine();

  if (are_friends) {
    if (ImGui::Button(remove_label.c_str(), half_width_btn)) {
      if (kernel_state()->friends_manager()->RemoveFriend(profile->xuid(),
                                                          friend_xuid)) {
        if (removed_xuid_) {
          *removed_xuid_ = friend_xuid;
        }

        ui::ShowFriendNotification(imgui_drawer, "Removed Friend",
                                   !gamertag.empty() ? gamertag : "Success");
      }
    }

    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Remove Friend");
    }
  } else {
    if (ImGui::Button(add_label.c_str(), half_width_btn)) {
      const bool added = kernel_state()->friends_manager()->AddFriend(
          profile->xuid(), friend_xuid);

      ui::ShowFriendNotification(
          imgui_drawer, "Added Friend",
          !added ? "Failed!" : (!gamertag.empty() ? gamertag : "Success"));
    }

    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Add Friend");
    }
  }

  return true;
}

bool xeDrawAddFriend(xe::ui::ImGuiDrawer* imgui_drawer, UserProfile* profile,
                     ui::AddFriendArgs& args) {
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImVec2 center = viewport->GetCenter();

  if (!args.add_friend_open) {
    args.add_friend_first_draw = false;
  }

  ImGui::SetNextWindowContentSize(ImVec2(200, 0));
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("Add Friend", &args.add_friend_open,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    if (!args.add_friend_context_open &&
        ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_GamepadFaceRight, false)) {
      ImGui::CloseCurrentPopup();
    }

    ImGui::SetWindowFontScale(1.05f);

    const bool max_friends = kernel_state()->friends_manager()->GetFriendsCount(
                                 profile->xuid()) >= X_ONLINE_MAX_FRIENDS;

    const std::string xuid_string = std::string(args.add_xuid_);

    uint64_t xuid = 0;

    args.valid_xuid = false;
    args.are_friends = false;

    bool is_self = false;

    if (xuid_string.length() == 16) {
      xuid = string_util::from_string<uint64_t>(xuid_string, true);

      args.valid_xuid = IsOnlineXUID(xuid);

      if (args.valid_xuid) {
        is_self = xuid == profile->GetOnlineXUID();
        args.are_friends =
            kernel_state()->friends_manager()->IsFriend(profile->xuid(), xuid);
      }
    }

    const bool can_add =
        args.valid_xuid && !args.are_friends && !is_self && !max_friends;

    ImGui::TextUnformatted("Friend's Online XUID:");

    ImGui::SameLine();

    const float window_width = ImGui::GetContentRegionAvail().x;
    const std::string friends_total = ui::FriendsCountString(profile->xuid());

    ImGui::SetCursorPosX((ImGui::GetCursorPosX() + window_width -
                          ImGui::CalcTextSize(friends_total.c_str()).x));

    if (max_friends) {
      ImGui::TextColored(ImColor(ui::kAddFriendErrorColor), "%s",
                         friends_total.c_str());
    } else {
      ImGui::TextDisabled("%s", friends_total.c_str());
    }

    if (!args.add_friend_first_draw && args.add_xuid_[0] == '\0') {
      args.add_friend_first_draw = true;
      ImGui::SetKeyboardFocusHere();
    }

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
    const bool submitted =
        ImGui::InputTextWithHint("##AddFriend", "0009XXXXXXXXXXXX",
                                 args.add_xuid_, sizeof(args.add_xuid_),
                                 ImGuiInputTextFlags_CharsHexadecimal |
                                     ImGuiInputTextFlags_CharsUppercase |
                                     ImGuiInputTextFlags_EnterReturnsTrue);

    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      ImGui::SetTooltip("Right click or press A to paste/clear");
    }

    if (ImGui::IsItemFocused() &&
        ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_GamepadFaceDown, false)) {
      ImGui::OpenPopup("##AddFriendContexts");
    }

    if (ImGui::BeginPopupContextItem("##AddFriendContexts")) {
      args.add_friend_context_open = true;

      if (ImGui::MenuItem("Paste")) {
        const char* clipboard = ImGui::GetClipboardText();

        if (!clipboard ||
            !ui::PasteXUID(args.add_xuid_, sizeof(args.add_xuid_), clipboard)) {
          ui::ShowFriendNotification(imgui_drawer, "Paste Failed",
                                     "Clipboard doesn't contain a valid XUID");
        }
      }

      ImGui::Separator();

      if (ImGui::MenuItem("Clear")) {
        memset(args.add_xuid_, 0, sizeof(args.add_xuid_));
      }

      ImGui::EndPopup();
    } else {
      args.add_friend_context_open = false;
    }

    // Always reserve the status line so the dialog doesn't resize while typing.
    if (max_friends) {
      ImGui::TextColored(ImColor(ui::kAddFriendErrorColor),
                         "Your friends list is full.");
    } else if (xuid_string.empty()) {
      ImGui::TextDisabled("Enter a 16 digit online XUID.");
    } else if (xuid_string.length() < 16) {
      ImGui::TextDisabled("%zu/16 digits", xuid_string.length());
    } else if (IsOfflineXUID(xuid)) {
      ImGui::TextColored(ImColor(ui::kAddFriendErrorColor),
                         "This is an offline XUID.");
    } else if (!args.valid_xuid) {
      ImGui::TextColored(ImColor(ui::kAddFriendErrorColor),
                         "Online XUIDs start with 0009.");
    } else if (is_self) {
      ImGui::TextColored(ImColor(ui::kAddFriendWarningColor),
                         "You can't add yourself.");
    } else if (args.are_friends) {
      ImGui::TextColored(ImColor(ui::kAddFriendWarningColor),
                         "Already on your friends list.");
    } else {
      ImGui::TextColored(ImColor(ui::kAddFriendValidColor), "Ready to add.");
    }

    const ImVec2 btn_size =
        ImVec2(ImGui::GetContentRegionAvail().x, ui::kFriendsButtonHeight);

    ImGui::BeginDisabled(!can_add);
    if (ImGui::Button("Add", btn_size) || (submitted && can_add)) {
      args.added_friend =
          kernel_state()->friends_manager()->AddFriend(profile->xuid(), xuid);

      ui::ShowFriendNotification(imgui_drawer, "Added Friend",
                                 args.added_friend ? xuid_string : "Failed!");

      if (args.added_friend) {
        memset(args.add_xuid_, 0, sizeof(args.add_xuid_));
        args.add_friend_first_draw = false;
      }
    }
    ImGui::EndDisabled();

    ImGui::EndPopup();
  }

  return true;
}

bool xeDrawFriendsContent(
    xe::ui::ImGuiDrawer* imgui_drawer, UserProfile* profile,
    ui::FriendsContentArgs& args, std::vector<X_ONLINE_FRIEND>& presences,
    std::map<uint64_t, std::shared_ptr<xe::ui::ImmediateTexture>>&
        immediate_gamerpics) {
  if (!profile) {
    return false;
  }

  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImVec2 center = viewport->GetCenter();

  ImGui::SetNextWindowSizeConstraints(ImVec2(400, 205), ImVec2(400, 600));
  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("Friends", &args.friends_open,
                             ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
    ImGui::SetWindowFontScale(1.05f);

    if (!args.add_friend_args.add_friend_open &&
        !args.add_friend_args.search_filter_context_open &&
        ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_GamepadFaceRight, false)) {
      ImGui::CloseCurrentPopup();
    }

    const float window_width = ImGui::GetContentRegionAvail().x;
    const ImVec2 half_width_btn = ui::HalfWidthButtonSize();

    ImGui::TextUnformatted("Search:");

    if (args.first_draw) {
      args.first_draw = false;
      ImGui::SetKeyboardFocusHere();
    }

    // Same as ImGuiTextFilter::Draw, but with a placeholder hint.
    ImGui::SetNextItemWidth(window_width);
    if (ImGui::InputTextWithHint("##Search", "Gamertag or XUID...",
                                 args.filter.InputBuf,
                                 IM_ARRAYSIZE(args.filter.InputBuf))) {
      args.filter.Build();
    }

    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Right Click/Gamepad A");
    }

    if (ImGui::IsItemFocused() &&
        ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_GamepadFaceDown, false)) {
      ImGui::OpenPopup("##SearchFilter");
    }

    if (ImGui::BeginPopupContextItem("##SearchFilter")) {
      args.add_friend_args.search_filter_context_open = true;

      if (ImGui::MenuItem("Paste")) {
        const char* clipboard = ImGui::GetClipboardText();

        if (clipboard) {
          xe::string_util::copy_truncating(args.filter.InputBuf, clipboard,
                                           sizeof(args.filter.InputBuf));

          args.filter.Build();
        }
      }

      ImGui::Separator();

      if (ImGui::MenuItem("Clear")) {
        memset(args.filter.InputBuf, 0, sizeof(args.filter.InputBuf));
        args.filter.Build();
      }

      ImGui::EndPopup();
    } else {
      args.add_friend_args.search_filter_context_open = false;
    }

    const uint32_t friends_count =
        kernel_state()->friends_manager()->GetFriendsCount(profile->xuid());
    const std::string friends_total = ui::FriendsCountString(profile->xuid());

    ImGui::TextUnformatted("Filters:");
    ImGui::SameLine(ImGui::GetCursorStartPos().x + window_width -
                    ImGui::CalcTextSize(friends_total.c_str()).x);
    ImGui::TextUnformatted(friends_total.c_str());

    ImGui::Checkbox("Joinable", &args.filter_joinable);
    ImGui::SameLine();
    ImGui::Checkbox("Same Game", &args.filter_title);
    ImGui::SameLine();
    ImGui::Checkbox("Hide Offline", &args.filter_offline);

    ImGui::Spacing();
    ImGui::Spacing();

    if (ImGui::Button("Add Friend", ImVec2(ImGui::GetContentRegionAvail().x,
                                           ui::kFriendsButtonHeight))) {
      args.add_friend_args.add_friend_open = true;
      ImGui::OpenPopup("Add Friend");
    }

    ImGui::BeginDisabled(!friends_count);
    if (ImGui::Button("Refresh", half_width_btn)) {
      args.refresh_presence = true;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    ImGui::BeginDisabled(!friends_count);
    if (ImGui::Button("Remove All", half_width_btn)) {
      ImGui::OpenPopup("Remove All Friends");
    }
    ImGui::EndDisabled();

    xeDrawAddFriend(imgui_drawer, profile, args.add_friend_args);

    if (args.add_friend_args.added_friend) {
      args.queue_refresh_presence = true;
      args.add_friend_args.added_friend = false;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const uint64_t own_online_xuid = profile->GetOnlineXUID();
    const uint32_t current_title_id = kernel_state()->title_id();

    uint64_t removed_xuid_ = 0;
    uint64_t previous_xuid = 0;
    uint64_t next_xuid = 0;
    uint32_t drawn_friends = 0;

    for (const auto& presence : presences) {
      const uint64_t friend_xuid = presence.xuid.get();

      if (friend_xuid == own_online_xuid) {
        continue;
      }

      if (args.filter.IsActive() &&
          !args.filter.PassFilter(presence.Gamertag) &&
          !args.filter.PassFilter(
              fmt::format("{:016X}", friend_xuid).c_str())) {
        continue;
      }

      const bool same_title = presence.title_id.get() == current_title_id;

      if (args.filter_joinable &&
          (!presence.session_id.as_uint64() || !same_title)) {
        continue;
      }

      if (args.filter_title && !same_title) {
        continue;
      }

      if (args.filter_offline &&
          (!presence.state.get() || !IsValidXUID(friend_xuid))) {
        continue;
      }

      std::shared_ptr<xe::ui::ImmediateTexture> gamerpic = {};

      if (const auto it = immediate_gamerpics.find(friend_xuid);
          it != immediate_gamerpics.end()) {
        gamerpic = it->second;
      }

      if (removed_xuid_ && !next_xuid) {
        next_xuid = friend_xuid;
      }

      if (friend_xuid == args.refocus_xuid) {
        ImGui::SetKeyboardFocusHere();
      }

      uint64_t selected_xuid_ = 0;
      xeDrawFriendContent(imgui_drawer, profile, gamerpic, presence,
                          &selected_xuid_, &removed_xuid_);

      if (!removed_xuid_) {
        previous_xuid = friend_xuid;
      }

      ImGui::Separator();
      ImGui::Spacing();
      ImGui::Spacing();

      drawn_friends++;
    }

    // The focused Remove button is gone after removing a friend, move focus to
    // the next card, or the previous one if it was the last.
    args.refocus_xuid = 0;

    if (removed_xuid_ && ImGui::GetIO().NavVisible) {
      args.refocus_xuid = next_xuid ? next_xuid : previous_xuid;
    }

    if (!drawn_friends) {
      const char* empty_text = !friends_count
                                   ? "No friends added yet."
                                   : "No friends match the current filters.";

      ImGui::Spacing();
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                           (window_width - ImGui::CalcTextSize(empty_text).x) *
                               0.5f);
      ImGui::TextDisabled("%s", empty_text);
      ImGui::Spacing();
    }

    // Erase after iterating, erasing inside the range-for invalidates it.
    if (removed_xuid_) {
      std::erase_if(presences, [removed_xuid_](const X_ONLINE_FRIEND& p) {
        return p.xuid.get() == removed_xuid_;
      });
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(225, 90), ImVec2(225, 90));
    if (ImGui::BeginPopupModal("Remove All Friends", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
      const ImVec2 btn_size = ui::HalfWidthButtonSize();

      const char* desc = "Are you sure?";

      ImGui::SetCursorPosX(
          (ImGui::GetWindowWidth() - ImGui::CalcTextSize(desc).x) * 0.5f);
      ImGui::TextUnformatted(desc);
      ImGui::Separator();

      if (ImGui::Button("Yes", btn_size)) {
        kernel_state()->friends_manager()->ClearFriends(profile->xuid());
        presences.clear();

        ui::ShowFriendNotification(imgui_drawer, "Removed All Friends",
                                   "Success");

        ImGui::CloseCurrentPopup();
      }

      ImGui::SameLine();

      if (ImGui::Button("Cancel", btn_size)) {
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    }

    ImGui::EndPopup();
  }

  return true;
}

bool xeDrawSessionContent(xe::ui::ImGuiDrawer* imgui_drawer,
                          UserProfile* profile,
                          std::unique_ptr<SessionObjectJSON>& session) {
  const uint32_t user_index =
      kernel_state()->xam_state()->GetUserIndexAssignedToProfileFromXUID(
          profile->GetOnlineXUID());

  const auto& title_version = kernel_state()->emulator()->title_version();

  const auto& media_id = kernel_state()
                             ->GetExecutableModule()
                             ->xex_module()
                             ->opt_execution_info()
                             ->media_id;

  const std::string mediaId_str = fmt::format("{:08X}", media_id.get());

  bool version_mismatch = title_version != session->Version();
  bool media_id_mismatch = mediaId_str != session->MediaID();

  uint32_t num_players =
      session->FilledPublicSlotsCount() + session->FilledPrivateSlotsCount();

  ImGui::Text(fmt::format("Players: {}", num_players).c_str());

  if (!session->Version().empty()) {
    ImGui::Text(fmt::format("Version: {}", session->Version()).c_str());
  }

  if (!session->MediaID().empty()) {
    ImGui::Text(fmt::format("Media ID: {}", session->MediaID()).c_str());
  }

  ImGui::Text(fmt::format("Open Private Slots: {}",
                          session->OpenPrivateSlotsCount().get())
                  .c_str());
  ImGui::Text(fmt::format("Open Public Slots: {}",
                          session->OpenPublicSlotsCount().get())
                  .c_str());

  ImGui::Spacing();
  ImGui::Spacing();

  const std::string join_label =
      std::format("Join Session##{}", session->SessionID());

  bool caller = MacAddress(session->MacAddress()) == GetConsoleMacAddress();

  std::string version_text = "Version mismatch!";
  std::string media_text = "Media ID mismatch!";

  auto version_width_btn = (ImGui::GetContentRegionAvail().x -
                            ImGui::CalcTextSize(version_text.c_str()).x) *
                           0.5f;

  auto media_id_width_btn = (ImGui::GetContentRegionAvail().x -
                             ImGui::CalcTextSize(media_text.c_str()).x) *
                            0.5f;

  ImGui::SetCursorPosX(version_width_btn);
  ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(240, 50, 50, 255));
  if (version_mismatch && !session->Version().empty()) {
    ImGui::Text("Version mismatch!");
  }

  ImGui::SetCursorPosX(media_id_width_btn);
  if (media_id_mismatch && !session->MediaID().empty()) {
    ImGui::Text("Media ID mismatch!");
  }
  ImGui::PopStyleColor();

  ImGui::Spacing();
  ImGui::Spacing();

  // What is player presence session is null?
  ImGui::BeginDisabled(!session->SessionID_UInt() || caller);
  if (ImGui::Button(join_label.c_str(),
                    ImVec2(ImGui::GetContentRegionAvail().x, 25))) {
    X_INVITE_INFO invite = {};

    invite.xuid_invitee = profile->GetOnlineXUID();
    invite.xuid_inviter = session->XUID_UInt();
    invite.title_id = kernel_state()->title_id();
    invite.from_game_invite = true;

    profile->SetSelfInvite(invite);

    kernel_state()->BroadcastNotification(kXNotificationLiveInviteAccepted,
                                          user_index);
  }
  ImGui::EndDisabled();

  if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
    if (caller) {
      ImGui::SetTooltip("Cannot join session from the same console.");
    } else {
      ImGui::SetTooltip("Join gaming session");
    }
  }

  return true;
}

bool xeDrawSessionsContent(
    xe::ui::ImGuiDrawer* imgui_drawer, UserProfile* profile,
    ui::SessionsContentArgs& sessions_args,
    std::vector<std::unique_ptr<SessionObjectJSON>>* sessions) {
  ImVec2 center = ImGui::GetMainViewport()->GetCenter();

  ImGui::SetNextWindowSizeConstraints(ImVec2(300, 150), ImVec2(300, 600));
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::BeginPopupModal("Sessions", &sessions_args.sessions_open,
                             ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
    ImGui::SetWindowFontScale(1.05f);

    if (ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_GamepadFaceRight, false)) {
      ImGui::CloseCurrentPopup();
    }

    bool in_game = kernel_state()->emulator()->title_id();

    if (in_game) {
      ImGui::Text(
          fmt::format("{}", kernel_state()->emulator()->title_name()).c_str());
    }

    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::Text(
        fmt::format("Available Sessions: {}", sessions->size()).c_str());

    ImGui::Spacing();
    ImGui::Spacing();

    ImGui::Checkbox("Hide My Sessions", &sessions_args.filter_own);

    ImGui::Spacing();

    if (ImGui::Button("Refresh",
                      ImVec2(ImGui::GetContentRegionAvail().x, 25))) {
      sessions->clear();
      sessions_args.refresh_sessions = true;
    }

    ImGui::Separator();

    ImGui::Spacing();
    ImGui::Spacing();

    if (sessions_args.refresh_sessions || sessions_args.refresh_sessions_sync) {
      auto run = [sessions]() {
        *sessions = kernel_state()->GetXboxLiveAPI()->GetTitleSessions();
      };

      if (sessions_args.refresh_sessions_sync) {
        run();

        sessions_args.refresh_sessions_sync = false;
      } else {
        std::thread refresh_sessions_thread(run);
        refresh_sessions_thread.detach();

        sessions_args.refresh_sessions = false;
      }
    }

    for (auto& session : *sessions) {
      bool caller = MacAddress(session->MacAddress()) == GetConsoleMacAddress();
      if (sessions_args.filter_own && caller) {
        continue;
      }

      xeDrawSessionContent(imgui_drawer, profile, session);

      ImGui::Separator();
      ImGui::Spacing();
      ImGui::Spacing();
    }

    ImGui::EndPopup();
  }

  return true;
}

bool xeDrawMyDeletedProfiles(
    xe::ui::ImGuiDrawer* imgui_drawer, ui::MyDeletedProfilesArgs& args,
    std::map<uint64_t, std::string>* deleted_profiles) {
  if (!deleted_profiles) {
    return false;
  }

  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImVec2 center = viewport->GetCenter();

  float btn_height = 25;
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSizeConstraints(ImVec2(250, 115), ImVec2(250, 415));
  if (ImGui::BeginPopupModal("Deleted Profiles", &args.deleted_profiles_open,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    if (ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_GamepadFaceRight, false)) {
      ImGui::CloseCurrentPopup();
    }

    float btn_width = (ImGui::GetContentRegionAvail().x * 0.5f) -
                      (ImGui::GetStyle().ItemSpacing.x * 0.5f);
    ImVec2 btn_size = ImVec2(btn_width, btn_height);

    const std::string desc =
        fmt::format("Deleted Profiles: {}", deleted_profiles->size());

    ImVec2 desc_size = ImGui::CalcTextSize(desc.c_str());

    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - desc_size.x) * 0.5f);
    ImGui::Text(desc.c_str());
    ImGui::Separator();
    ImGui::Spacing();

    for (const auto& [xuid, gamertag] : *deleted_profiles) {
      ImGui::Spacing();
      ImGui::Spacing();

      std::string xuid_str = fmt::format("XUID: {:016X}", xuid);
      std::string gamertag_str = fmt::format("Gamertag: {}", gamertag);

      ImGui::Text(xuid_str.c_str());
      ImGui::Text(gamertag_str.c_str());

      ImGui::Separator();
    }

    ImGui::EndPopup();
  }

  return true;
}

void xeDrawUPnPAndPorts(xe::ui::ImGuiDrawer* imgui_drawer,
                        ui::UPnPAndPortsArgs& args, UPnP* upnp) {
  ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImVec2 center = viewport->GetCenter();

  float btn_height_padding = ImGui::GetStyle().FramePadding.x * 2.5f;
  float btn_width_padding = ImGui::GetStyle().FramePadding.x * 5.0f;

  auto centre_text = [](std::string text) {
    ImVec2 text_size = ImGui::CalcTextSize(text.c_str());
    ImGui::SetCursorPosX((ImGui::GetWindowWidth() - text_size.x) * 0.5f);
    ImGui::Text(text.c_str());
    ImGui::Separator();
  };

  if (args.first_draw) {
    args.first_draw = false;

    if (cvars::upnp && upnp) {
      upnp->Start();
    }
  }

  float btn_height = 25;
  ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSizeConstraints(ImVec2(500, 0), ImVec2(500, 500));
  if (ImGui::BeginPopupModal("UPnP and Ports", &args.dialog_open,
                             ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove)) {
    if (ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_GamepadFaceRight, false)) {
      ImGui::CloseCurrentPopup();
    }

    std::string lbl_toggle_upnp = cvars::upnp ? "Disable UPnP" : "Enable UPnP";
    ImVec2 toggle_upnp_lbl_size = ImGui::CalcTextSize(lbl_toggle_upnp.c_str());
    ImVec2 toggle_upnp_btn_size =
        ImVec2(toggle_upnp_lbl_size.x + btn_width_padding,
               toggle_upnp_lbl_size.y + btn_height_padding);

    if (ImGui::Button(lbl_toggle_upnp.c_str(), toggle_upnp_btn_size)) {
      const bool upnp_state = !cvars::upnp;

      UPnP::SetUPnPState(upnp_state);

      if (upnp) {
        if (upnp_state) {
          if (!upnp->IsActive()) {
            upnp->Start();
          }

          upnp->OpenTrackedPorts();
        } else {
          upnp->CloseOpenPorts();
        }
      }
    }

    if (cvars::upnp && upnp && upnp->IsActive() &&
        kernel_state()->emulator()->is_title_open()) {
      std::string lbl_refresh_ports = "Refresh Ports";
      ImVec2 refresh_ports_lbl_size =
          ImGui::CalcTextSize(lbl_refresh_ports.c_str());
      ImVec2 refresh_upnp_btn_size =
          ImVec2(refresh_ports_lbl_size.x + btn_width_padding,
                 refresh_ports_lbl_size.y + btn_height_padding);

      ImGui::SameLine();

      ImGui::BeginDisabled(upnp->GetOpenedPorts().empty());
      if (ImGui::Button("Refresh Ports", refresh_upnp_btn_size)) {
        upnp->RefreshPorts();
      }
      ImGui::EndDisabled();
    }

    ImGui::SameLine();

    std::string lbl_wiki_desc = "UPnP & Port Forwarding";
    ImVec2 wiki_desc_size = ImGui::CalcTextSize(lbl_wiki_desc.c_str());

    const float pos_x = ImGui::GetCursorPosX();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         ImGui::GetContentRegionAvail().x - wiki_desc_size.x);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() -
                         ImGui::GetStyle().ItemSpacing.y);
    ImGui::TextLinkOpenURL(lbl_wiki_desc.c_str(),
                           "https://github.com/AdrianCassar/xenia-canary/"
                           "wiki/UPnP-&-Port-Forwarding");
    ImGui::SetCursorPosX(pos_x);

    ImGui::Spacing();
    ImGui::Separator();

    if (!upnp) {
      centre_text("UPnP failed to initialize!");
      ImGui::EndPopup();
      return;
    }

    if (cvars::upnp) {
      if (upnp->IsActive()) {
        ImGui::Text("UPnP device found!");
        ImGui::Spacing();
      } else {
        ImGui::Text(
            "No UPnP devices discovered! Please check UPnP is supported or "
            "enabled on your router.");
        ImGui::Spacing();
      }
    }

    if (!kernel_state()->emulator()->is_title_open()) {
      centre_text("Game is not running!");
      ImGui::EndPopup();
      return;
    }

    if (kernel_state()->emulator()->is_title_open()) {
      if (!cvars::upnp || !upnp->IsActive()) {
        if (!upnp->GetTrackedPorts().empty()) {
          ImGui::Text("Tracked Ports:");
          ImGui::Spacing();

          const auto tracked_ports = upnp->GetTrackedPorts();

          if (ImGui::BeginTable("##PortsTable", 2,
                                ImGuiTableFlags_SizingStretchProp |
                                    ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("Protocol");
            ImGui::TableSetupColumn("Port");

            ImGui::TableHeadersRow();

            for (const auto& [protocol, internal_ports] : tracked_ports) {
              for (const auto& internal_port : internal_ports) {
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::Text(protocol.c_str());

                ImGui::TableNextColumn();
                ImGui::Text(fmt::format("{}", internal_port).c_str());
              }
            }
            ImGui::EndTable();
          }
        } else {
          centre_text("No ports are tracked!");
        }
      } else if (upnp->IsActive()) {
        if (!upnp->GetTrackedPorts().empty()) {
          const auto tracked_ports = upnp->GetTrackedPorts();

          ImGui::Spacing();

          if (upnp->IsVariableLeaseSupported()) {
            ImGui::TextWrapped(
                "Your router supports variable UPnP lease times. UPnP ports "
                "have "
                "a lease time of 1 hour, every 45 minutes UPnP will reset the "
                "lease time to 1 hour.");
          } else {
            ImGui::TextWrapped(
                "Your router only supports permanent lease times.");
          }

          ImGui::Spacing();

          ImGui::TextWrapped("All ports are closed upon exit.");

          ImGui::Spacing();
          ImGui::Spacing();

          if (ImGui::BeginTable("##PortsTable", 5,
                                ImGuiTableFlags_SizingStretchProp |
                                    ImGuiTableFlags_Borders)) {
            ImGui::TableSetupColumn("Protocol");
            ImGui::TableSetupColumn("Port");
            ImGui::TableSetupColumn("Error Code");
            ImGui::TableSetupColumn("Status");
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed,
                                    150.0f);

            ImGui::TableHeadersRow();

            const auto port_results = upnp->GetPortBindingResults();
            const auto opened_ports = upnp->GetOpenedPorts();

            for (const auto& [protocol, internal_ports] : tracked_ports) {
              for (const auto& internal_port : internal_ports) {
                bool is_port_open = false;

                if (opened_ports.contains(protocol)) {
                  if (opened_ports.at(protocol).contains(internal_port)) {
                    is_port_open = true;
                  }
                }

                int32_t error_code = -1;

                if (port_results.contains(protocol)) {
                  if (port_results.at(protocol).contains(internal_port)) {
                    error_code = port_results.at(protocol).at(internal_port);
                  }
                }

                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                ImGui::Text(protocol.c_str());

                ImGui::TableNextColumn();
                ImGui::Text(fmt::format("{}", internal_port).c_str());

                ImGui::TableNextColumn();
                ImGui::Text(fmt::format("{}", error_code).c_str());

                ImGui::TableNextColumn();
                std::string error_status;

                if (!is_port_open && error_code == -1) {
                  error_status = "Port Closed";
                } else {
                  if (error_code < 0) {
                    error_status =
                        UPnP::GetMiniUPnPcErrorCodeToDesc(error_code);
                  } else {
                    error_status = UPnP::GetUPnPErrorCodeToDesc(error_code);
                  }
                }

                ImGui::TextWrapped(error_status.c_str());

                ImGui::TableNextColumn();

                float width = (ImGui::GetContentRegionAvail().x -
                               ImGui::GetStyle().ItemSpacing.x) /
                              2.0f;

                std::string lbl_open_port = "Open";
                ImVec2 open_port_lbl_size =
                    ImGui::CalcTextSize(lbl_open_port.c_str());
                ImVec2 open_ports_btn_size =
                    ImVec2(width, open_port_lbl_size.y + btn_height_padding);

                std::string open_lbl = fmt::format("{}## {}({})", lbl_open_port,
                                                   internal_port, protocol);

                ImGui::BeginDisabled(is_port_open);
                if (ImGui::Button(open_lbl.c_str(), open_ports_btn_size)) {
                  upnp->AddPort(upnp->GetLocalIP(), internal_port, protocol);
                }
                ImGui::EndDisabled();

                ImGui::SameLine();

                std::string lbl_close_port = "Close";
                ImVec2 close_port_lbl_size =
                    ImGui::CalcTextSize(lbl_close_port.c_str());
                ImVec2 close_port_btn_size =
                    ImVec2(width, close_port_lbl_size.y + btn_height_padding);

                std::string lbl_close = fmt::format(
                    "{}## {}({})", lbl_close_port, internal_port, protocol);

                ImGui::BeginDisabled(!is_port_open);
                if (ImGui::Button(lbl_close.c_str(), close_port_btn_size)) {
                  upnp->RemovePort(internal_port, protocol);
                }
                ImGui::EndDisabled();
              }
            }
            ImGui::EndTable();
          }
        } else {
          centre_text("No ports have opened!");
        }
      }
    }

    ImGui::EndPopup();
  }
}

X_RESULT xeXamShowSigninUI(uint32_t user_index, uint32_t users_needed,
                           uint32_t flags) {
  // Mask values vary. Probably matching user types? Local/remote?
  // Games seem to sit and loop until we trigger sign in notification.
  if (users_needed != 1 && users_needed != 2 && users_needed != 4) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (cvars::headless) {
    return xeXamDispatchHeadlessAsync([users_needed]() {
      std::map<uint8_t, uint64_t> xuids;

      for (uint32_t i = 0; i < XUserMaxUserCount; i++) {
        UserProfile* profile = kernel_state()->xam_state()->GetUserProfile(i);
        if (profile) {
          xuids[i] = profile->xuid();
          if (xuids.size() >= users_needed) {
            break;
          }
        }
      }

      kernel_state()->xam_state()->profile_manager()->LoginMultiple(xuids);
    });
  }

  if (kernel_state()->xam_state()->IsUIActive()) {
    return X_ERROR_ACCESS_DENIED;
  }

  kernel_state()->xam_state()->is_xam_dialog_present_.store(true);

  auto close = [](ui::SigninUI* dialog) -> void {};

  const Emulator* emulator = kernel_state()->emulator();
  xe::ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();
  return xeXamDispatchDialogAsync<ui::SigninUI>(
      new ui::SigninUI(
          imgui_drawer, kernel_state()->xam_state()->profile_manager(),
          emulator->input_system()->GetLastUsedSlot(), users_needed, flags),
      close);
}

X_RESULT xeXamShowCreateProfileUIEx(uint32_t user_index, dword_t flag,
                                    char* unkn2_ptr) {
  Emulator* emulator = kernel_state()->emulator();
  xe::ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();

  if (cvars::headless) {
    return X_ERROR_SUCCESS;
  }

  if (kernel_state()->xam_state()->IsUIActive()) {
    return X_ERROR_ACCESS_DENIED;
  }

  kernel_state()->xam_state()->is_xam_dialog_present_.store(true);

  auto close = [](ui::CreateProfileUI* dialog) -> void {};

  return xeXamDispatchDialogAsync<ui::CreateProfileUI>(
      new ui::CreateProfileUI(imgui_drawer, emulator), close);
}

dword_result_t XamShowSigninUI_entry(dword_t users_needed, dword_t flags) {
  return xeXamShowSigninUI(XUserIndexAny, users_needed, flags);
}
DECLARE_XAM_EXPORT1(XamShowSigninUI, kUserProfiles, kImplemented);

dword_result_t XamShowSigninUIEx_entry(
    dword_t users_needed, dword_t flags,
    pointer_t<XAM_OVERLAPPED> overlapped_ptr) {
  X_RESULT result = xeXamShowSigninUI(XUserIndexAny, users_needed, flags);
  if (overlapped_ptr) {
    kernel_state()->CompleteOverlappedImmediate(overlapped_ptr, result);
    return X_ERROR_IO_PENDING;
  } else {
    return result;
  }
}
DECLARE_XAM_EXPORT1(XamShowSigninUIEx, kUserProfiles, kSketchy);

dword_result_t XamShowNuiSigninUI_entry(dword_t tracking_id, dword_t user_index,
                                        dword_t flags) {
  auto result = kernel_state()->nui()->NuiHudCheck(tracking_id);
  if (result) {
    return result;
  }
  uint32_t users_needed = 1;
  uint32_t sent_flags = flags | static_cast<uint32_t>(SigninUiFlags::NUI);
  // xeXamNuiHudCheck(tracking_id) = success then continue else return
  return xeXamShowSigninUI(user_index, users_needed, sent_flags);
}
DECLARE_XAM_EXPORT1(XamShowNuiSigninUI, kUserProfiles, kSketchy);

dword_result_t XamShowSigninUIp_entry(dword_t user_index, dword_t users_needed,
                                      dword_t flags) {
  return xeXamShowSigninUI(user_index, users_needed, flags);
}
DECLARE_XAM_EXPORT1(XamShowSigninUIp, kUserProfiles, kImplemented);

dword_result_t XamShowCreateProfileUIEx_entry(dword_t user_index, dword_t flag,
                                              lpstring_t unkn2_ptr) {
  return xeXamShowCreateProfileUIEx(user_index, flag, unkn2_ptr);
}
DECLARE_XAM_EXPORT1(XamShowCreateProfileUIEx, kUserProfiles, kImplemented);

dword_result_t XamShowCreateProfileUI_entry(dword_t user_index, dword_t flag) {
  return xeXamShowCreateProfileUIEx(user_index, flag, 0);
}
DECLARE_XAM_EXPORT1(XamShowCreateProfileUI, kUserProfiles, kImplemented);

dword_result_t XamShowAchievementsUI_entry(dword_t user_index,
                                           dword_t title_id) {
  auto user = kernel_state()->xam_state()->GetUserProfile(user_index);
  if (!user) {
    return X_ERROR_NO_SUCH_USER;
  }

  uint32_t proper_title_id =
      title_id ? title_id.value()
               : kernel_state()->xam_state()->spa_info()->title_id();

  const auto info =
      kernel_state()->xam_state()->user_tracker()->GetUserTitleInfo(
          user->xuid(), proper_title_id);

  if (!info) {
    return X_ERROR_NO_SUCH_USER;
  }

  xe::ui::ImGuiDrawer* imgui_drawer =
      kernel_state()->emulator()->imgui_drawer();

  auto close = [](ui::GameAchievementsUI* dialog) -> void {};
  return xeXamDispatchDialogAsync<ui::GameAchievementsUI>(
      new ui::GameAchievementsUI(imgui_drawer, ImVec2(100.f, 100.f),
                                 &info.value(), user),
      close);
}
DECLARE_XAM_EXPORT1(XamShowAchievementsUI, kUserProfiles, kStub);

dword_result_t XamShowGamerCardUI_entry(dword_t user_index) {
  auto user = kernel_state()->xam_state()->GetUserProfile(user_index);
  if (!user) {
    return X_ERROR_ACCESS_DENIED;
  }

  xe::ui::ImGuiDrawer* imgui_drawer =
      kernel_state()->emulator()->imgui_drawer();

  auto close = [](ui::GamercardUI* dialog) -> void {};
  return xeXamDispatchDialogAsync<ui::GamercardUI>(
      new ui::GamercardUI(kernel_state()->emulator()->display_window(),
                          imgui_drawer, kernel_state(), user->xuid()),
      close);
}
DECLARE_XAM_EXPORT1(XamShowGamerCardUI, kUserProfiles, kImplemented);

dword_result_t XamShowEditProfileUI_entry(dword_t user_index) {
  auto user = kernel_state()->xam_state()->GetUserProfile(user_index);
  if (!user) {
    return X_ERROR_ACCESS_DENIED;
  }

  xe::ui::ImGuiDrawer* imgui_drawer =
      kernel_state()->emulator()->imgui_drawer();

  auto close = [](ui::GamercardUI* dialog) -> void {};
  return xeXamDispatchDialogAsync<ui::GamercardUI>(
      new ui::GamercardUI(kernel_state()->emulator()->display_window(),
                          imgui_drawer, kernel_state(), user->xuid()),
      close);
}
DECLARE_XAM_EXPORT1(XamShowEditProfileUI, kUserProfiles, kImplemented);

dword_result_t XamShowGamerCardUIForXUID_entry(dword_t user_index,
                                               qword_t xuid_player) {
  uint64_t xuid = xuid_player;

  if (user_index >= XUserMaxUserCount) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (IsGuestXUID(xuid)) {
    return X_ERROR_INVALID_PARAMETER;
  }

  auto user = kernel_state()->xam_state()->GetUserProfile(user_index);
  if (!user) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (!xuid) {
    xuid = user->xuid();
  }

  if (!IsValidXUID(xuid)) {
    return X_ERROR_INVALID_PARAMETER;
  }

  if (IsOnlineXUID(xuid) &&
      !kernel_state()->xam_state()->user_tracker()->LoggedInToLive()) {
    return X_ERROR_ACCESS_DENIED;
  }

  if (xuid || xuid == user->xuid() || xuid == user->GetOnlineXUID()) {
    if (kernel_state()->xam_state()->IsUIActive()) {
      return X_ERROR_ACCESS_DENIED;
    }

    kernel_state()->xam_state()->is_xam_dialog_present_.store(true);

    auto close = [](ui::GamercardFromXUIDUI* dialog) -> void {};

    const Emulator* emulator = kernel_state()->emulator();
    xe::ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();
    return xeXamDispatchDialogAsync<ui::GamercardFromXUIDUI>(
        new ui::GamercardFromXUIDUI(imgui_drawer, xuid, user), close);
  }

  return X_ERROR_INVALID_PARAMETER;
}
DECLARE_XAM_EXPORT1(XamShowGamerCardUIForXUID, kUserProfiles, kStub);

dword_result_t XamShowFriendsUI_entry(dword_t user_index) {
  if (user_index >= XUserMaxUserCount && user_index != XUserIndexAny) {
    return X_ERROR_FUNCTION_FAILED;
  }

  UserProfile* user = nullptr;

  if (user_index == XUserIndexAny) {
    if (kernel_state()
            ->xam_state()
            ->profile_manager()
            ->IsAnyProfileSignedIn()) {
      user =
          kernel_state()->xam_state()->GetUserProfile(static_cast<uint32_t>(0));
    }
  } else {
    user = kernel_state()->xam_state()->GetUserProfile(user_index);
  }

  if (!user) {
    return X_ERROR_FUNCTION_FAILED;
  }

  const Emulator* emulator = kernel_state()->emulator();
  xe::ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();

  auto close = [](ui::FriendsUI* dialog) -> void {};

  return xeXamDispatchDialogAsync<ui::FriendsUI>(
      new ui::FriendsUI(imgui_drawer, user), close);
}
DECLARE_XAM_EXPORT1(XamShowFriendsUI, kUserProfiles, kImplemented);

dword_result_t XamShowCommunitySessionsUI_entry(dword_t user_index,
                                                dword_t social_sessions_flags) {
  if (user_index >= XUserMaxUserCount && user_index != XUserIndexAny) {
    return X_ERROR_FUNCTION_FAILED;
  }

  UserProfile* user = nullptr;

  if (user_index == XUserIndexAny) {
    if (kernel_state()
            ->xam_state()
            ->profile_manager()
            ->IsAnyProfileSignedIn()) {
      user =
          kernel_state()->xam_state()->GetUserProfile(static_cast<uint32_t>(0));
    }
  } else {
    user = kernel_state()->xam_state()->GetUserProfile(user_index);
  }

  if (!user) {
    return X_ERROR_FUNCTION_FAILED;
  }

  const Emulator* emulator = kernel_state()->emulator();
  xe::ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();

  auto close = [](ui::ShowCommunitySessionsUI* dialog) -> void {};

  return xeXamDispatchDialogAsync<ui::ShowCommunitySessionsUI>(
      new ui::ShowCommunitySessionsUI(imgui_drawer, user), close);
}
DECLARE_XAM_EXPORT1(XamShowCommunitySessionsUI, kUserProfiles, kImplemented);

// UI
dword_result_t XamShowNuiTroubleshooterUI_entry(dword_t user_index,
                                                dword_t tracking_id,
                                                dword_t flags) {
  /* Notes:
     - calls XamPackageManagerGetExperienceMode(&var) with var = 1
     - If returns less than zero or (var & 1) == 0 then get error message:
       - if XamPackageManagerGetExperienceMode = 0 then call XamShowMessageBoxUI
         - if XamShowMessageBoxUI returns 0x3e5 then XamShowNuiTroubleshooterUI
     returns 0
       - else XamShowNuiTroubleshooterUI returns 0x65b and call another func
     - else:
       - call XamNuiHudSetEngagedTrackingID(tracking_id) and doesn't care aboot
     return and set var2 = 2
       - checks if (flag & 0x800000) == 0
         - if true call XamNuiGetDeviceStatus.
           - if XamNuiGetDeviceStatus != 0 set var2 = 3
       - else var2 = 4
       - XamAppRequestLoadEx(var2);
       - if return = 0 then XamShowNuiTroubleshooterUI returns 5
       - else set buffer[8] and call
     XMsgSystemProcessCall(0xfe,0x21028,buffer,0xc);
     - XamNuiNatalCameraUpdateComplete calls
     XamShowNuiTroubleshooterUI(0xff,0,0) if param = -0x7ff8fffe
  */

  if (cvars::headless) {
    return 0;
  }

  const Emulator* emulator = kernel_state()->emulator();
  xe::ui::Window* display_window = emulator->display_window();
  xe::ui::ImGuiDrawer* imgui_drawer = emulator->imgui_drawer();
  if (display_window && imgui_drawer) {
    xe::threading::Fence fence;
    if (display_window->app_context().CallInUIThreadSynchronous([&]() {
          xe::ui::ImGuiDialog::ShowMessageBox(
              imgui_drawer, "NUI Troubleshooter",
              "The game has indicated there is a problem with NUI (Kinect).")
              ->Then(&fence);
        })) {
      kernel_state()->xam_state()->is_xam_dialog_present_.store(true);
      fence.Wait();
      kernel_state()->xam_state()->is_xam_dialog_present_.store(false);
    }
  }

  return X_ERROR_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamShowNuiTroubleshooterUI, kNone, kStub);

dword_result_t XamShowNuiHardwareRequiredUI_entry(unknown_t unk1) {
  if (unk1 != 0) {
    return X_ERROR_INVALID_PARAMETER;
  }

  return XamShowNuiTroubleshooterUI_entry(0xff, 0, 0x400000);
}
DECLARE_XAM_EXPORT1(XamShowNuiHardwareRequiredUI, kNone, kImplemented);

dword_result_t XamShowNuiGuideUI_entry(unknown_t tracking_id, unknown_t unk2) {
  auto result = kernel_state()->nui()->NuiHudCheck(tracking_id);
  if (result) {
    return result;
  }
  /* Notes:
   - calls xeXamNuiHudCheck, if success call XamAppRequestLoadEx(4) and if
   succeed call XMsgSystemProcessCall and some sub function that ensures correct
   h_result is returned
   - if XamAppRequestLoadEx(4) fails return X_ERROR_ACCESS_DENIED
  */

  return X_ERROR_ACCESS_DENIED;
}
DECLARE_XAM_EXPORT1(XamShowNuiGuideUI, kNone, kStub);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(UI);
