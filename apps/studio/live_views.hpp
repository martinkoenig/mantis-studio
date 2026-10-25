#pragma once
#include "preview.hpp"
#include <QPointer>
#include <QQuickItem>
#include <mantis/render.hpp>

// One QObject owner for live buffers; visual hosts never acquire deletion ownership.
class LiveViews : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject *bridge READ bridge WRITE setBridge NOTIFY bridgeChanged)
    Q_PROPERTY(QQuickItem *cloud READ cloud CONSTANT)
    Q_PROPERTY(QQuickItem *leftView READ left CONSTANT)
    Q_PROPERTY(QQuickItem *rightView READ right CONSTANT)
    Q_PROPERTY(QQuickItem *cloudHost READ cloudHost WRITE setCloudHost NOTIFY hostsChanged)
    Q_PROPERTY(QQuickItem *leftHost READ leftHost WRITE setLeftHost NOTIFY hostsChanged)
    Q_PROPERTY(QQuickItem *rightHost READ rightHost WRITE setRightHost NOTIFY hostsChanged)
  public:
    explicit LiveViews(QQuickItem *parent = nullptr);
    ~LiveViews() override;
    QObject *bridge() const {
        return bridge_;
    }
    void setBridge(QObject *);
    QQuickItem *cloud() const {
        return cloud_;
    }
    QQuickItem *left() const {
        return left_;
    }
    QQuickItem *right() const {
        return right_;
    }
    QQuickItem *cloudHost() const {
        return cloud_host_;
    }
    QQuickItem *leftHost() const {
        return left_host_;
    }
    QQuickItem *rightHost() const {
        return right_host_;
    }
    void setCloudHost(QQuickItem *);
    void setLeftHost(QQuickItem *);
    void setRightHost(QQuickItem *);
  signals:
    void bridgeChanged();
    void hostsChanged();

  private:
    void host(QQuickItem *, QPointer<QQuickItem> &, QList<QMetaObject::Connection> &, QQuickItem *);
    QPointer<QObject> bridge_;
    mantis::render::PointCloudView *cloud_;
    MeasurementView *left_, *right_;
    QPointer<QQuickItem> cloud_host_, left_host_, right_host_;
    QList<QMetaObject::Connection> cloud_connections_, left_connections_, right_connections_;
};
