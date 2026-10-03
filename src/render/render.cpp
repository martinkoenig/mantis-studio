#include <QPainter>
#include <cmath>
#include <cstring>
#include <mantis/render.hpp>
namespace mantis::render {
PointCloudView::PointCloudView(QQuickItem *parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
}
void PointCloudView::setCloud(data::Published packet) {
    if (packet) {
        auto cloud = data::point_cloud(*packet);
        if (!cloud)
            throw Failure(cloud.error());
    }
    packet_ = std::move(packet);
    emit cloudChanged();
    update();
}
void PointCloudView::setYaw(double v) {
    yaw_ = v;
    emit viewChanged();
    update();
}
void PointCloudView::setPitch(double v) {
    pitch_ = std::clamp(v, -85.0, 85.0);
    emit viewChanged();
    update();
}
void PointCloudView::setZoom(double v) {
    zoom_ = std::clamp(v, 0.2, 6.0);
    emit viewChanged();
    update();
}
int PointCloudView::pointCount() const {
    if (!packet_)
        return 0;
    for (auto &a : packet_->attributes)
        if (a.descriptor.name == "org.mantis.position")
            return static_cast<int>(a.descriptor.shape[0]);
    return 0;
}
void PointCloudView::paint(QPainter *painter) {
    painter->fillRect(boundingRect(), QColor("#101820"));
    double scale = std::min(width() / 100.0, height() / 85.0) * zoom_;
    double yaw = yaw_ * 3.141592653589793 / 180, pitch = pitch_ * 3.141592653589793 / 180;
    auto project = [&](double x, double y, double z) {
        double a = std::cos(yaw) * x - std::sin(yaw) * y, b = std::sin(yaw) * x + std::cos(yaw) * y;
        return QPointF(width() / 2 + a * scale,
                       height() / 2 + (std::sin(pitch) * b - std::cos(pitch) * z) * scale);
    };
    painter->setPen(QPen(QColor("#20303d"), 1));
    for (int i = -50; i <= 50; i += 10) {
        painter->drawLine(project(i, -50, 0), project(i, 50, 0));
        painter->drawLine(project(-50, i, 0), project(50, i, 0));
    }
    if (packet_)
        for (const auto &a : packet_->attributes)
            if (a.descriptor.name == "org.mantis.position" &&
                a.descriptor.scalar == schema::ScalarType::f32 && a.descriptor.shape.size() == 2 &&
                a.descriptor.shape[1] == 3) {
                auto mapped = a.buffer.map_read();
                if (!mapped)
                    continue;
                for (uint64_t i = 0; i < a.descriptor.shape[0]; ++i) {
                    float xyz[3];
                    for (size_t j = 0; j < 3; ++j)
                        std::memcpy(xyz + j,
                                    mapped->data() + i * a.descriptor.stride[0] + j * a.descriptor.stride[1],
                                    4);
                    auto color =
                        QColor::fromHsvF(0.45F + std::clamp(xyz[2] / 90.0F, 0.0F, 0.2F), 0.6F, 0.95F);
                    painter->setPen(QPen(color, 2.2));
                    painter->drawPoint(project(xyz[0], xyz[1], xyz[2]));
                }
            }
    for (auto [axis, color] : std::initializer_list<std::pair<int, QColor>>{
             {0, QColor("#ec7783")}, {1, QColor("#74d5a4")}, {2, QColor("#81b6ff")}}) {
        double v[3]{};
        v[axis] = 15;
        painter->setPen(QPen(color, 2));
        painter->drawLine(project(-42, -32, 0), project(-42 + v[0], -32 + v[1], v[2]));
        painter->drawText(project(-42 + v[0], -32 + v[1], v[2]) + QPointF(5, 0), QString("XYZ").mid(axis, 1));
    }
}
} // namespace mantis::render
