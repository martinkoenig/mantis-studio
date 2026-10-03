#pragma once
#include <QQuickPaintedItem>
#include <mantis/data.hpp>
namespace mantis::render {
class PointCloudView : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(double yaw READ yaw WRITE setYaw NOTIFY viewChanged)
    Q_PROPERTY(double pitch READ pitch WRITE setPitch NOTIFY viewChanged)
    Q_PROPERTY(double zoom READ zoom WRITE setZoom NOTIFY viewChanged)
    Q_PROPERTY(int pointCount READ pointCount NOTIFY cloudChanged)
    data::Published packet_;
    double yaw_{-35}, pitch_{35}, zoom_{1};

  public:
    explicit PointCloudView(QQuickItem *parent = nullptr);
    void setCloud(data::Published);
    void paint(QPainter *) override;
    double yaw() const {
        return yaw_;
    }
    double pitch() const {
        return pitch_;
    }
    double zoom() const {
        return zoom_;
    }
    void setYaw(double);
    void setPitch(double);
    void setZoom(double);
    int pointCount() const;
  signals:
    void cloudChanged();
    void viewChanged();
};
} // namespace mantis::render
