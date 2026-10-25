#pragma once
#include <QObject>
#include <QPointer>
#include <QVariantMap>

// Metadata presentation only: no client, transport, timer or mutable runtime action.
class ProjectsModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *bridge READ bridge WRITE setBridge NOTIFY bridgeChanged)
    Q_PROPERTY(QVariantMap data READ data NOTIFY changed)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY filtersChanged)
    Q_PROPERTY(QString typeFilter READ typeFilter WRITE setTypeFilter NOTIFY filtersChanged)
    Q_PROPERTY(QString stateFilter READ stateFilter WRITE setStateFilter NOTIFY filtersChanged)
    Q_PROPERTY(QString sort READ sort WRITE setSort NOTIFY filtersChanged)
  public:
    explicit ProjectsModel(QObject *parent = nullptr);
    QObject *bridge() const {
        return bridge_;
    }
    void setBridge(QObject *);
    QVariantMap data() const {
        return data_;
    }
    QString query() const {
        return query_;
    }
    QString typeFilter() const {
        return type_;
    }
    QString stateFilter() const {
        return state_;
    }
    QString sort() const {
        return sort_;
    }
    void setQuery(QString);
    void setTypeFilter(QString);
    void setStateFilter(QString);
    void setSort(QString);
    Q_INVOKABLE void clearFilters();
    Q_INVOKABLE void selectArtifact(const QString &id);
  public slots:
    void refresh();
  signals:
    void bridgeChanged();
    void changed();
    void filtersChanged();

  private:
    void present();
    QPointer<QObject> bridge_;
    QVariantMap base_, data_;
    QVariantList artifacts_;
    QString identity_, selected_, query_, type_, state_, sort_ = "ID";
};
