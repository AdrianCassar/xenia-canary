#include "xenia/ui/qt/tabs/home_tab.h"

#include <QFIleDialog>
#include <QGraphicsEffect>
#include <QProgressBar>

#include "xenia/app/emulator_window.h"
#include "xenia/base/logging.h"
#include "xenia/ui/qt/actions/action.h"
#include "xenia/ui/qt/main_window.h"
#include "xenia/ui/qt/widgets/separator.h"
#include "xenia/ui/qt/widgets/slider.h"

namespace xe {
namespace ui {
namespace qt {

HomeTab::HomeTab() : XTab("Home", "HomeTab") { Build(); }

void HomeTab::Build() {
  layout_ = new QHBoxLayout();
  layout_->setContentsMargins(0, 0, 0, 0);
  layout_->setSpacing(0);
  setLayout(layout_);

  BuildSidebar();
  BuildRecentView();
}

void HomeTab::BuildSidebar() {
  // sidebar container widget
  sidebar_ = new QWidget(this);
  sidebar_->setObjectName("sidebarContainer");

  QVBoxLayout* sidebar_layout = new QVBoxLayout;
  sidebar_layout->setContentsMargins(0, 0, 0, 0);
  sidebar_layout->setSpacing(0);

  sidebar_->setLayout(sidebar_layout);

  // Add drop shadow to sidebar widget
  QGraphicsDropShadowEffect* effect = new QGraphicsDropShadowEffect;
  effect->setBlurRadius(16);
  effect->setXOffset(4);
  effect->setYOffset(0);
  effect->setColor(QColor(0, 0, 0, 64));

  sidebar_->setGraphicsEffect(effect);

  // Create sidebar
  sidebar_toolbar_ = new XSideBar;
  sidebar_toolbar_->setOrientation(Qt::Vertical);
  sidebar_toolbar_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  sidebar_toolbar_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);

  // Create sidebar title
  QWidget* sidebar_title = new QWidget;
  sidebar_title->setObjectName("sidebarTitle");

  QVBoxLayout* title_layout = new QVBoxLayout;
  title_layout->setContentsMargins(0, 40, 0, 0);
  title_layout->setSpacing(0);

  sidebar_title->setLayout(title_layout);

  // Title labels
  QLabel* xenia_title = new QLabel("Xenia");
  xenia_title->setObjectName("sidebarTitleLabel");

  QLabel* xenia_subtitle = new QLabel("Xbox 360 Emulator");
  xenia_subtitle->setObjectName("sidebarSubtitleLabel");

  title_layout->addWidget(xenia_title, 0, Qt::AlignHCenter | Qt::AlignBottom);
  title_layout->addWidget(xenia_subtitle, 0, Qt::AlignHCenter | Qt::AlignTop);

  // Title separator
  auto separator = new XSeparator;
  title_layout->addWidget(separator, 0, Qt::AlignHCenter);

  // Setup Sidebar toolbar
  sidebar_toolbar_->addWidget(sidebar_title);

  sidebar_toolbar_->addSpacing(20);

  auto open_file_btn = sidebar_toolbar_->addAction(QChar(0xE838), "Open File");
  connect(open_file_btn, &XSideBarButton::clicked, this,
          &HomeTab::OpenFileTriggered);

  auto import_folder_btn = sidebar_toolbar_->addAction(QChar(0xE8F4), "Import Folder");
  connect(import_folder_btn, &XSideBarButton::clicked, this,
          &HomeTab::ImportFolderTriggered);

  sidebar_toolbar_->addSeparator();

  sidebar_layout->addWidget(sidebar_toolbar_, 0,
                            Qt::AlignHCenter | Qt::AlignTop);
  sidebar_layout->addStretch(1);

  // Add sidebar to tab widget
  layout_->addWidget(sidebar_, 0, Qt::AlignLeft);
}

void HomeTab::BuildRecentView() {
  // Create container widget
  QWidget* recent_container = new QWidget(this);

  QVBoxLayout* recent_layout = new QVBoxLayout(this);
  recent_layout->setContentsMargins(0, 0, 0, 0);
  recent_layout->setSpacing(0);

  recent_container->setLayout(recent_layout);

  // Setup toolbar
  auto toolbar = recent_toolbar_;
  toolbar = new XToolBar(this);

  QLabel* title = new QLabel("Recent Games");
  title->setObjectName("recentGames");

  toolbar->addWidget(title);

  toolbar->addSeparator();

  // TODO: handle button clicks
  auto play_btn = toolbar->addAction(new XAction(QChar(0xEDB5), "Play"));
  connect(play_btn, &XToolBarItem::clicked, [this]() { PlayTriggered(); });

  toolbar->addAction(new XAction(QChar(0xEBE8), "Debug"));
  toolbar->addAction(new XAction(QChar(0xE946), "Info"));

  toolbar->addSeparator();

  toolbar->addAction(new XAction(QChar(0xE8FD), "List"));
  toolbar->addAction(new XAction(QChar(0xF0E2), "Grid"));

  // TODO: hide slider unless "Grid" mode is selected

  auto* slider = new XSlider(Qt::Horizontal, this);
  slider->setRange(48, 96);
  slider->setFixedWidth(100);
  toolbar->addWidget(slider);

  recent_layout->addWidget(toolbar);

  // Create recent games list view
  // TODO: this should only be shown when "List" mode selected in toolbar
  // and should also only load games from a "recent" cache

  list_view_ = new XGameListView(this);
  recent_layout->addWidget(list_view_);

  layout_->addWidget(recent_container);

  // Lower the widget to prevent overlap with sidebar's shadow
  recent_container->lower();
}

void HomeTab::PlayTriggered() {
  // Get path from table and launch game in new EmulatorWindow
  // This is purely a proof of concept
  auto index = list_view_->selectionModel();
  if (index->hasSelection()) {
    QModelIndexList path_row =
        index->selectedRows(static_cast<int>(GameColumn::kPathColumn));

    const QModelIndex& path_index = path_row.at(0);

    QString path = path_index.data().toString();

    // TODO: Launch target at `path`
  }
}

void HomeTab::OpenFileTriggered() {
  QString file_name = QFileDialog::getOpenFileName(
      this, "Open Game", "",
      tr("All Xbox 360 Files (*.xex *.iso);;Xbox 360 Executable (*.xex);;Disc "
         "Image (*.iso);;All Files (*)"));
  if (!file_name.isEmpty()) {
    GameLibrary& lib = GameLibrary::Instance();
    lib.ScanPath(file_name.toUtf8().constData());

    list_view_->RefreshGameList();
  }
}

void HomeTab::ImportFolderTriggered() {
  QString path = QFileDialog::getExistingDirectory(this, "Open Folder", "");
  if (!path.isEmpty()) {
    QWidget* progress_widget = new QWidget();
    QHBoxLayout* layout = new QHBoxLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(16);
    progress_widget->setLayout(layout);

    QLabel* label = new QLabel("Scanning directories...");
    label->setStyleSheet("color: #c7c7c7");
    layout->addWidget(label);

    QProgressBar* bar = new QProgressBar();
    bar->setFixedSize(120, 16);
    bar->setRange(0, 100);
    bar->setValue(0);
    bar->setTextVisible(false);
    layout->addWidget(bar);

    layout->addStretch();

    MainWindow* window = qobject_cast<MainWindow*>(this->window());
    window->AddStatusBarWidget(progress_widget);

    GameLibrary& lib = GameLibrary::Instance();
    lib.ScanPathAsync(path.toUtf8().constData(),
                      [=](double progress, const GameEntry& entry) {
                        // update progress bar on main UI thread
                        QMetaObject::invokeMethod(
                            bar,
                            [=]() {
                              bar->setValue(progress);
                              if (progress == 100.0) {
                                window->RemoveStatusBarWidget(progress_widget);
                              }
                            },
                            Qt::QueuedConnection);
                        // Just a PoC. In future change to refresh list
                        // when all games added.
                        list_view_->RefreshGameList();
                      });
  }
}

}  // namespace qt
}  // namespace ui
}  // namespace xe
