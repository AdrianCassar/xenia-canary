#include "xenia/ui/qt/delegates/game_listview_delegate.h"
#include "xenia/ui/qt/models/game_library_model.h"

#include <QBitmap>
#include <QImage>
#include <QPainter>

namespace xe {
namespace ui {
namespace qt {

XGameListViewDelegate::XGameListViewDelegate(QWidget* parent)
    : QStyledItemDelegate(parent) {
  QImage mask_image(":resources/graphics/GameIconMask.png");
  icon_mask_ = QBitmap::fromImage(mask_image.createAlphaMask());
}

void XGameListViewDelegate::paint(QPainter* painter,
                                  const QStyleOptionViewItem& option,
                                  const QModelIndex& index) const {
  painter->save();
  auto options = QStyleOptionViewItem(option);
  options.state &= (~QStyle::State_HasFocus);

  initStyleOption(&options, index);
  GameColumn column = (GameColumn)index.column();
  switch (column) {
    case GameColumn::kIconColumn: {
      if (index.data().canConvert<QImage>()) {
        QStyledItemDelegate::paint(painter, options, index);
        QImage image = qvariant_cast<QImage>(index.data());
        QPixmap pixmap = QPixmap::fromImage(image);
        pixmap.setDevicePixelRatio(painter->device()->devicePixelRatioF());
        paintIcon(pixmap, painter, options, index);
      }
      break;
    }
    default: {
      QStyledItemDelegate::paint(painter, options, index);
    }
  }
  painter->restore();
}

void XGameListViewDelegate::paintIcon(QPixmap& icon, QPainter* painter,
                                      const QStyleOptionViewItem& options,
                                      const QModelIndex& index) const {
  painter->save();

  // Get the column bounds
  double col_x = options.rect.x();
  double col_y = options.rect.y();
  double col_width = options.rect.width();
  double col_height = options.rect.height();
  double icon_size = options.rect.height() * 0.8;

  icon.setMask(icon_mask_);

  // Calculate the Icon position
  QRectF rect = icon.rect();
  QRectF icon_rect = QRectF(rect.x(), rect.y(), icon_size, icon_size);
  double shift_x = (col_width - icon_size) / 2 + col_x;
  double shift_y = (col_height - icon_size) / 2 + col_y;
  icon_rect.translate(shift_x, shift_y);

  // adding QPainter::Antialiasing here smoothes masked edges
  // but makes the image look slightly blurry
  painter->setRenderHints(QPainter::SmoothPixmapTransform);
  painter->drawPixmap(icon_rect, icon, icon.rect());

  painter->restore();
}

QSize XGameListViewDelegate::sizeHint(const QStyleOptionViewItem& option,
                                      const QModelIndex& index) const {
  GameColumn column = (GameColumn)index.column();

  switch (column) {
    case GameColumn::kIconColumn:
      return QSize(58, 48);
    case GameColumn::kPathColumn: {
      return QSize(500, 48);
    }
    default:
      return QSize(96, 48);
  }
}

}  // namespace qt
}  // namespace ui
}  // namespace xe
