#ifndef XENIA_UI_QT_GAME_LISTVIEW_DELEGATE_H_
#define XENIA_UI_QT_GAME_LISTVIEW_DELEGATE_H_

#include <QPixmap>
#include <QStyledItemDelegate>

namespace xe {
namespace ui {
namespace qt {

class XGameListViewDelegate : public QStyledItemDelegate {
  Q_OBJECT

 public:
  explicit XGameListViewDelegate(QWidget* parent = nullptr);

  void paint(QPainter* painter, const QStyleOptionViewItem& option,
             const QModelIndex& index) const override;
  QSize sizeHint(const QStyleOptionViewItem& options,
                 const QModelIndex& index) const override;

 private:
  void paintIcon(QPixmap& icon, QPainter* painter,
                 const QStyleOptionViewItem& options,
                 const QModelIndex& index) const;

  QBitmap icon_mask_;
};

}  // namespace qt
}  // namespace ui
}  // namespace xe

#endif