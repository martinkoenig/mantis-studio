#pragma once
#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <mantis/client.hpp>

// GUI-thread, read-only snapshot projection. Owns no transport, worker, timer or device commands.
class DevicesModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *bridge READ bridge WRITE setBridge NOTIFY bridgeChanged)
    Q_PROPERTY(QVariantList nodes READ nodes NOTIFY graphChanged)
    Q_PROPERTY(QVariantMap data READ data NOTIFY changed)
  public:
    explicit DevicesModel(QObject *parent = nullptr);
    QObject *bridge() const {
        return bridge_;
    }
    void setBridge(QObject *);
    QVariantList nodes() const {
        return snapshot_.value("nodes").toList();
    }
    QVariantMap data() const {
        return data_;
    }
    Q_INVOKABLE void selectDevice(const QString &id);
    // Recheck authority at intent time, including accessible/direct signal invocation.
    Q_INVOKABLE QString calibrationIdentity() const;
  public slots:
    void observeSnapshot(const mantis::wire::v1::Response &);
    void refresh();
  signals:
    void bridgeChanged();
    void changed();
    void graphChanged();

  private:
    void present();
    QPointer<QObject> bridge_;
    QVariantMap snapshot_, data_;
    QString selected_, project_;
    QByteArray fingerprint_;
    bool seen_{};
};
