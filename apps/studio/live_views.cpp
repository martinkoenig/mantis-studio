#include "live_views.hpp"
#include <QQmlEngine>
#include <QThread>

LiveViews::LiveViews(QQuickItem *parent)
    : QQuickItem(parent), cloud_(new mantis::render::PointCloudView(this)), left_(new MeasurementView(this)),
      right_(new MeasurementView(this)) {
    for (auto *view : {static_cast<QQuickItem *>(cloud_), static_cast<QQuickItem *>(left_),
                       static_cast<QQuickItem *>(right_)}) {
        QQmlEngine::setObjectOwnership(view, QQmlEngine::CppOwnership);
        view->setSize({320, 240});
        view->setVisible(false);
    }
    cloud_->setObjectName("pointCloudView");
    left_->setObjectName("leftPreview");
    right_->setObjectName("rightPreview");
}
LiveViews::~LiveViews() {
    // Hosts may already be gone during window teardown. No new host transition here.
    for (auto *view : {static_cast<QQuickItem *>(cloud_), static_cast<QQuickItem *>(left_),
                       static_cast<QQuickItem *>(right_)})
        view->setParentItem(nullptr);
    // QObject's child destruction deletes the three C++-owned views exactly once.
}
void LiveViews::setBridge(QObject *value) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (value && value->thread() != thread())
        return;
    if (value == bridge_)
        return;
    if (bridge_) {
        QMetaObject::invokeMethod(bridge_, "attachView", Qt::DirectConnection, Q_ARG(QObject *, nullptr));
        QMetaObject::invokeMethod(bridge_, "attachPreview", Qt::DirectConnection, Q_ARG(QObject *, nullptr),
                                  Q_ARG(QObject *, nullptr));
    }
    bridge_ = value;
    if (bridge_) {
        QMetaObject::invokeMethod(bridge_, "attachView", Qt::DirectConnection, Q_ARG(QObject *, cloud_));
        QMetaObject::invokeMethod(bridge_, "attachPreview", Qt::DirectConnection, Q_ARG(QObject *, left_),
                                  Q_ARG(QObject *, right_));
    }
    emit bridgeChanged();
}
void LiveViews::host(QQuickItem *view, QPointer<QQuickItem> &current,
                     QList<QMetaObject::Connection> &connections, QQuickItem *next) {
    Q_ASSERT(QThread::currentThread() == thread());
    if (current == next)
        return;
    // A visual host never changes the QObject parent or the engine ownership.
    if (next && next->thread() != thread())
        return;
    if (next && window() && next->window() && window() != next->window())
        return;
    for (const auto &connection : connections)
        disconnect(connection);
    connections.clear();
    current = next;
    view->setParentItem(next ? next : this);
    view->setPosition({0, 0});
    view->setVisible(next != nullptr);
    if (next) {
        const auto size = [view, target = QPointer<QQuickItem>(next)] {
            if (target)
                view->setSize({qMax(1.0, target->width()), qMax(1.0, target->height())});
        };
        connections.push_back(connect(next, &QQuickItem::widthChanged, this, size));
        connections.push_back(connect(next, &QQuickItem::heightChanged, this, size));
        size();
    } else {
        view->setSize({320, 240});
    }
    emit hostsChanged();
}
void LiveViews::setCloudHost(QQuickItem *value) {
    host(cloud_, cloud_host_, cloud_connections_, value);
}
void LiveViews::setLeftHost(QQuickItem *value) {
    host(left_, left_host_, left_connections_, value);
}
void LiveViews::setRightHost(QQuickItem *value) {
    host(right_, right_host_, right_connections_, value);
}
