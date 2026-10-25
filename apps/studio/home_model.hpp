#pragma once
#include <QObject>
#include <QPointer>
#include <QVariantMap>

// Internal, read-only presentation. No client, command API, timer or runtime ownership.
class HomeModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *bridge READ bridge WRITE setBridge NOTIFY bridgeChanged)
    Q_PROPERTY(QVariantMap data READ data NOTIFY changed)
  public:
    explicit HomeModel(QObject *parent = nullptr);
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
    bool seenSnapshot_{};
};
