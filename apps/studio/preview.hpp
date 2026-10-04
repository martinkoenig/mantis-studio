#pragma once
#include <QImage>
#include <QPainter>
#include <QQuickPaintedItem>
class MeasurementView : public QQuickPaintedItem {
    Q_OBJECT
    QImage image_;
  public:
    explicit MeasurementView(QQuickItem *parent = nullptr) : QQuickPaintedItem(parent) {}
    void setImage(QImage image) { image_ = std::move(image); update(); }
    bool ready() const { return !image_.isNull() && width() > 20 && height() > 20; }
    void paint(QPainter *painter) override {
        painter->fillRect(boundingRect(), QColor("#10151c"));
        if (image_.isNull()) return;
        auto size = image_.size(); size.scale(boundingRect().size().toSize(), Qt::KeepAspectRatio);
        QRectF target{QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), QSizeF(size)};
        painter->drawImage(target, image_);
    }
};
struct PreviewResult { QImage left, right; std::string error; };
