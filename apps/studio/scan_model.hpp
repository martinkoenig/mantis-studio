#pragma once
#include <QObject>
#include <QPointer>
#include <QVariantMap>

// Read-only frontend metadata. No client, timer, command, packet or preview ownership.
class ScanModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *bridge READ bridge WRITE setBridge NOTIFY bridgeChanged)
    Q_PROPERTY(QVariantMap data READ data NOTIFY changed)
  public:
    explicit ScanModel(QObject *parent = nullptr);
    QObject *bridge() const {
        return bridge_;
    }
    void setBridge(QObject *);
    QVariantMap data() const {
        return data_;
    }
  public slots:
    void refresh();
  signals:
    void bridgeChanged();
    void changed();

  private:
    QPointer<QObject> bridge_;
    QVariantMap data_;
    QString project_identity_;
    quint64 project_epoch_{};
};
