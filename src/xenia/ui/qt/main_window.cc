#include "xenia/ui/qt/main_window.h"

#include <QVBoxLayout>

#include "build/version.h"
#include "xenia/ui/qt/widgets/status_bar.h"

namespace xe {
namespace ui {
namespace qt {

bool MainWindow::OpenImpl() {
  // Custom Frame Border
  // Disable for now until windows aero additions are added
  // setWindowFlags(Qt::Window | Qt::FramelessWindowHint);

  this->setFocusPolicy(Qt::StrongFocus);

  shell_ = new XShell(this);
  this->setCentralWidget(shell_);

  status_bar_ = new XStatusBar(this);
  this->setStatusBar(status_bar_);

  QLabel* build_label = new QLabel;
  build_label->setObjectName("buildLabel");
  build_label->setText(QStringLiteral("Xenia: %1 / %2 / %3")
                           .arg(XE_BUILD_BRANCH)
                           .arg(XE_BUILD_COMMIT_SHORT)
                           .arg(XE_BUILD_DATE));
  status_bar_->addPermanentWidget(build_label);

  QtWindow::OpenImpl();
  return true;
}

void MainWindow::AddStatusBarWidget(QWidget* widget, bool permanent) {
  if (permanent) {
    status_bar_->addPermanentWidget(widget);
  } else {
    status_bar_->addWidget(widget);
  }
}

void MainWindow::RemoveStatusBarWidget(QWidget* widget) {
  return status_bar_->removeWidget(widget);
}

}  // namespace qt
}  // namespace ui
}  // namespace xe