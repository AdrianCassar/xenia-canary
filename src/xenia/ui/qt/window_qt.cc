/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "window_qt.h"

#include <QMenu>
#include <QScreen>
#include <QWindow>

#include "xenia/base/logging.h"

namespace xe {
namespace ui {
namespace qt {

QtWindow::QtWindow(WindowedAppContext& app_context, std::string_view title,
                   uint32_t width, uint32_t height)
    : Window(app_context, title, width, height) {}

QtWindow::~QtWindow() { EnterDestructor(); }

uint32_t QtWindow::GetLatestDpiImpl() const {
  return static_cast<uint32_t>(screen()->logicalDotsPerInch());
}

bool QtWindow::OpenImpl() {
  // TODO(Razzile):
  // I have weird bugs converting directly from std::string to QString
  // possibly a standard library mismatch between Qt and my VC++ install
  // so I have to create QStrings from const char*
  setWindowTitle(GetTitle().c_str());

  TryLoadMenuItems(GetMainMenu());

  resize(static_cast<int>(GetDesiredLogicalWidth()),
         static_cast<int>(GetDesiredLogicalHeight()));

  show();

  {
    WindowDestructionReceiver destruction_receiver(this);
    OnActualSizeUpdate(width(), height(), destruction_receiver);

    if (destruction_receiver.IsWindowDestroyedOrClosed()) {
      return true;
    }
  }

  return true;
}

void QtWindow::TryLoadMenuItems(MenuItem* root_item) {
  // TODO: implement menu item loading
}

void QtWindow::RequestCloseImpl() { close(); }

void QtWindow::ApplyNewFullscreen() {
  auto win_states = windowHandle()->windowStates();
  if (IsFullscreen()) {
    main_menu_enabled_ = false;
    win_states |= Qt::WindowFullScreen;
  } else {
    win_states &= ~Qt::WindowFullScreen;
  }
  windowHandle()->setWindowStates(win_states);
}

void QtWindow::ApplyNewTitle() { setWindowTitle(GetTitle().c_str()); }

void QtWindow::LoadAndApplyIcon(const void* buffer, size_t size,
                                bool can_apply_state_in_current_phase) {
  QPixmap pixmap;
  if (pixmap.loadFromData(static_cast<const uint8_t*>(buffer),
                          static_cast<uint32_t>(size))) {
    return setWindowIcon(QIcon(pixmap));
  }
  XELOGW("Failed to set window icon");
}

void QtWindow::ApplyNewMainMenu(MenuItem* old_main_menu) {
  TryLoadMenuItems(GetMainMenu());
}

void QtWindow::ApplyNewCursorVisibility(
    CursorVisibility old_cursor_visibility) {
  auto new_visibility = GetCursorVisibility();
  if (new_visibility == CursorVisibility::kHidden) {
    setCursor(Qt::BlankCursor);
  } else if (new_visibility == CursorVisibility::kVisible) {
    setCursor(Qt::ArrowCursor);
  } else {
    XELOGW("auto-hide cursor mode not supported in Qt window");
    setCursor(Qt::ArrowCursor);
  }
}

void QtWindow::FocusImpl() { windowHandle()->requestActivate(); }

SurfacePtr QtWindow::CreateSurfaceImpl(Surface::TypeFlags allowed_types) {
  // base Qt window should have no surface, instead a specialized subclass
  // should provide a surface
  return nullptr;
}

void QtWindow::RequestPaintImpl() { repaint(); }

void QtMenuItem::OnChildAdded(MenuItem* generic_child_item) {
  assert(qt_menu_ != nullptr);

  auto child_item = static_cast<QtMenuItem*>(generic_child_item);

  switch (child_item->type()) {
    case Type::kPopup: {
      child_item->qt_menu_ =
          new QMenu(QString::fromStdString(child_item->text()));
      qt_menu_->addMenu(child_item->qt_menu_);
    } break;

    case Type::kSeparator: {
      qt_menu_->addSeparator();
    } break;

    case Type::kString: {
      auto title = QString::fromStdString(child_item->text());
      auto hotkey = child_item->hotkey();
      auto shortcut =
          hotkey.empty() ? 0 : QKeySequence(QString::fromStdString(hotkey));

      qt_menu_->addAction(title, shortcut,
                          [child_item]() { child_item->OnSelected(); });
    } break;
    case Type::kNormal: {
      // TODO?
    } break;
    default:
      break;
  }
}

bool QtWindow::event(QEvent* event) {
  switch (event->type()) {
    case QEvent::Close: {
      WindowDestructionReceiver destruction_receiver(this);
      OnBeforeClose(destruction_receiver);
      if (destruction_receiver.IsWindowDestroyed()) {
        return true;
      }
      OnAfterClose();

      break;
    }
    default:
      break;
  }

  return QMainWindow::event(event);
}

void QtWindow::changeEvent(QEvent* event) { QMainWindow::changeEvent(event); }

void QtWindow::dragEnterEvent(QDragEnterEvent* event) {
  QMainWindow::dragEnterEvent(event);
}

void QtWindow::dropEvent(QDropEvent* event) { QMainWindow::dropEvent(event); }

}  // namespace qt
}  // namespace ui
}  // namespace xe
