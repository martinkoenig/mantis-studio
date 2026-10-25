#pragma once
#include <QObject>
#include <QPointer>
#include <QVariantMap>

// Read-only runtime metadata with local validated device selection. No client, timer, command, packet or
// preview ownership.
class ScanModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *bridge READ bridge WRITE setBridge NOTIFY bridgeChanged)
    Q_PROPERTY(QVariantMap data READ data NOTIFY changed)
    Q_PROPERTY(QString selectedDevice READ selectedDevice WRITE setSelectedDevice NOTIFY changed)
  public:
    explicit ScanModel(QObject *parent = nullptr);
    QObject *bridge() const {
        return bridge_;
    }
    void setBridge(QObject *);
    QString selectedDevice() const {
        return selected_device_;
    }
    void setSelectedDevice(QString);
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
    QString project_identity_, selected_device_;
    quint64 project_epoch_{};
};
