/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#undef UNICODE
#undef _UNICODE

#include <QApplication>
#include <cstdlib>
#include <memory>

#include <QCoreApplication>
#include <QFontDatabase>
#include <QGuiApplication>

#include "main_window.h"
#include "windowed_app_context_qt.h"

#include "xenia/app/settings/settings.h"
#include "xenia/base/console.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/main_win.h"
#include "xenia/base/platform.h"
#include "xenia/base/platform_win.h"
#include "xenia/ui/windowed_app.h"

DEFINE_bool(enable_console, false, "Open a console window with the main window",
            "General");

using xe::app::settings::Settings;
using xe::ui::qt::QtWindowedAppContext;

int main(int argc, char** argv) {
  Q_INIT_RESOURCE(xenia);

  QCoreApplication::setApplicationName("Xenia");
  QCoreApplication::setOrganizationName(
      "Xenia Xbox 360 Emulator Research Project");
  QCoreApplication::setOrganizationDomain("https://xenia.jp");

  QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
      Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

  QApplication qt_app(argc, argv);

  // Load Fonts
  QFontDatabase fonts;
  fonts.addApplicationFont(":/resources/fonts/SegMDL2.ttf");
  fonts.addApplicationFont(":/resources/fonts/segoeui.ttf");
  fonts.addApplicationFont(":/resources/fonts/segoeuisb.ttf");
  QApplication::setFont(QFont("Segoe UI"));

  if (xe::cvars::enable_console) {
    xe::AttachConsole();
  }

  QtWindowedAppContext app_context;

  std::unique_ptr<xe::ui::WindowedApp> app =
      xe::ui::GetWindowedAppCreator()(app_context);

#if XE_PLATFORM_WIN32
  xe::InitializeWin32App(app->GetName());
#endif

  int result = app->OnInitialize() ? app_context.RunMainMessageLoop(qt_app)
                                   : EXIT_FAILURE;

  app->InvokeOnDestroy();

  // int result = qt_app.exec();

  return result;
}