#pragma once
#include <QImage>
#include <QPainter>
#include <QQuickPaintedItem>
class MeasurementView : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY imageChanged)
    Q_PROPERTY(QSize imageSize READ imageSize NOTIFY imageChanged)
    QImage image_;

  public:
    explicit MeasurementView(QQuickItem *parent = nullptr) : QQuickPaintedItem(parent) {}
    void setImage(QImage image) {
        const bool metadataChanged = image_.isNull() != image.isNull() || image_.size() != image.size();
        image_ = std::move(image);
        if (metadataChanged)
            emit imageChanged();
        update();
    }
    bool hasImage() const {
        return !image_.isNull();
    }
    QSize imageSize() const {
        return image_.size();
    }
    const QImage &image() const {
        return image_;
    } // Internal read-only verification seam; no pixel copy.

    bool ready() const {
        return !image_.isNull() && width() > 20 && height() > 20;
    }
    void paint(QPainter *painter) override {
        painter->fillRect(boundingRect(), QColor("#10151c"));
        if (image_.isNull())
            return;
        auto size = image_.size();
        size.scale(boundingRect().size().toSize(), Qt::KeepAspectRatio);
        QRectF target{QPointF((width() - size.width()) / 2, (height() - size.height()) / 2), QSizeF(size)};
        painter->drawImage(target, image_);
    }
  signals:
    void imageChanged();
};
struct PreviewResult {
    QImage left, right;
    std::string error;
    quint64 projectGeneration{};
    QString source;
};
