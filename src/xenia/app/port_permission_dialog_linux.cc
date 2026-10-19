/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Xenia Canary. All rights reserved.                          *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/app/port_permission_dialog.h"

#include <linux/capability.h>
#include <spawn.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cfloat>
#include <fstream>

#include "third_party/imgui/imgui.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/kernel/xnet.h"

extern char** environ;

DEFINE_bool(prompt_privileged_ports, true,
            "Prompt to allow binding ports below 1024.", "Live");

DECLARE_int32(network_mode);

namespace xe {
namespace app {

namespace {

constexpr int kRequiredPortStart = 1000;

constexpr char kSysctlPath[] = "/proc/sys/net/ipv4/ip_unprivileged_port_start";

// Fixed commands, never built from user input.
constexpr char kPersistentFixCommand[] =
    "echo net.ipv4.ip_unprivileged_port_start=1000 > "
    "/etc/sysctl.d/99-xenia-netplay.conf && "
    "sysctl -q -p /etc/sysctl.d/99-xenia-netplay.conf";

constexpr char kSessionFixCommand[] =
    "sysctl -q -w net.ipv4.ip_unprivileged_port_start=1000";

// pkexec exit codes
constexpr int kPkexecDismissed = 126;
constexpr int kPkexecNotAuthorized = 127;
constexpr int kSpawnFailed = -1;

int GetUnprivilegedPortStart() {
  std::ifstream file(kSysctlPath);
  int value = -1;
  if (!(file >> value)) {
    return -1;
  }
  return value;
}

bool HasNetBindCapability() {
  if (geteuid() == 0) {
    return true;
  }

  __user_cap_header_struct header = {_LINUX_CAPABILITY_VERSION_3, 0};
  __user_cap_data_struct data[_LINUX_CAPABILITY_U32S_3] = {};

  if (syscall(SYS_capget, &header, data) != 0) {
    return false;
  }

  return data[CAP_TO_INDEX(CAP_NET_BIND_SERVICE)].effective &
         CAP_TO_MASK(CAP_NET_BIND_SERVICE);
}

int RunPkexecShell(const char* command) {
  char* argv[] = {const_cast<char*>("pkexec"), const_cast<char*>("/bin/sh"),
                  const_cast<char*>("-c"), const_cast<char*>(command), nullptr};

  pid_t pid = 0;
  if (posix_spawnp(&pid, "pkexec", nullptr, nullptr, argv, environ) != 0) {
    return kSpawnFailed;
  }

  int status = 0;
  if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status)) {
    return kSpawnFailed;
  }

  return WEXITSTATUS(status);
}

}  // namespace

PortPermissionDialog::PortPermissionDialog(ui::ImGuiDrawer* imgui_drawer)
    : ui::ImGuiDialog(imgui_drawer) {}

bool PortPermissionDialog::CanBindRequiredPorts() {
  const int port_start = GetUnprivilegedPortStart();

  // Unknown kernel config, assume it's fine rather than nagging.
  if (port_start < 0) {
    return true;
  }

  return port_start <= kRequiredPortStart || HasNetBindCapability();
}

void PortPermissionDialog::ShowIfNeeded(ui::ImGuiDrawer* imgui_drawer) {
  if (!cvars::prompt_privileged_ports ||
      cvars::network_mode == kernel::NETWORK_MODE::OFFLINE) {
    return;
  }

  if (CanBindRequiredPorts()) {
    return;
  }

  XELOGW(
      "Ports below {} are privileged (ip_unprivileged_port_start={}), titles "
      "may fail to bind network ports.",
      kRequiredPortStart, GetUnprivilegedPortStart());

  new PortPermissionDialog(imgui_drawer);
}

void PortPermissionDialog::RunFix(bool persistent) {
  const char* command = persistent ? kPersistentFixCommand : kSessionFixCommand;

  status_message_ = "Waiting for authentication...";
  fix_result_ = std::async(std::launch::async,
                           [command]() { return RunPkexecShell(command); });
}

bool PortPermissionDialog::PollFix() {
  if (!fix_result_.valid() || fix_result_.wait_for(std::chrono::seconds(0)) !=
                                  std::future_status::ready) {
    return false;
  }

  const int result = fix_result_.get();

  if (CanBindRequiredPorts()) {
    XELOGI("Privileged port binding enabled (ip_unprivileged_port_start={})",
           GetUnprivilegedPortStart());
    Close();
    return true;
  }

  switch (result) {
    case kSpawnFailed:
      status_message_ = "Failed to run pkexec. Install polkit and try again.";
      break;
    case kPkexecDismissed:
      status_message_ = "Authentication was cancelled.";
      break;
    case kPkexecNotAuthorized:
      status_message_ = "Not authorized.";
      break;
    default:
      status_message_ = fmt::format("Command failed (exit code {}).", result);
      break;
  }

  XELOGW("Privileged port fix failed: {}", status_message_);
  return false;
}

void PortPermissionDialog::OnDraw(ImGuiIO& io) {
  if (PollFix()) {
    return;
  }

  const bool fix_running = fix_result_.valid();

  ImGui::SetNextWindowPos(
      ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
      ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowBgAlpha(1.0f);

  const float window_width = ImGui::GetFontSize() * 40.0f;
  ImGui::SetNextWindowSizeConstraints(ImVec2(window_width, 0.0f),
                                      ImVec2(window_width, FLT_MAX));

  bool dialog_open = true;
  if (!ImGui::Begin(
          "Network Port Permissions", &dialog_open,
          ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::End();
    return;
  }

  ImGui::TextWrapped(
      "Linux restricts low ports to root by default.\nWithout permission, "
      "games may fail to host or join sessions.");

  ImGui::NewLine();

  ImGui::TextWrapped(
      "Allow unprivileged programs to use ports 1000 and above? You will be "
      "asked for your password.");

  ImGui::NewLine();

  ImGui::BeginDisabled(fix_running);

  constexpr int button_count = 3;
  const float button_width =
      (ImGui::GetContentRegionAvail().x -
       ImGui::GetStyle().ItemSpacing.x * (button_count - 1)) /
      button_count;
  const ImVec2 button_size(button_width, 0.0f);

  if (ImGui::Button("Fix permanently", button_size)) {
    RunFix(true);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Writes /etc/sysctl.d/99-xenia-netplay.conf");
  }

  ImGui::SameLine();
  if (ImGui::Button("Fix for this session", button_size)) {
    RunFix(false);
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip("Resets on reboot");
  }

  ImGui::SameLine();

  if (ImGui::Button("Don't ask again", button_size)) {
    OVERRIDE_bool(prompt_privileged_ports, false);
    dialog_open = false;
  }

  ImGui::EndDisabled();

  if (!status_message_.empty()) {
    ImGui::NewLine();
    ImGui::TextWrapped("%s", status_message_.c_str());
  }

  ImGui::End();

  // Keep the dialog alive while pkexec is running so the future isn't
  // destroyed (which would block the UI thread).
  if (!dialog_open && !fix_running) {
    Close();
  }
}

}  // namespace app
}  // namespace xe
