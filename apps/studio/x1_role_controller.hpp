#pragma once
#include "x1_role_client.hpp"
#include <QFutureWatcher>
#include <QObject>
#include <QVariantMap>
#include <atomic>

class X1RoleController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
    Q_PROPERTY(QString deviceId READ deviceId WRITE setDeviceId NOTIFY changed)
    Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY changed)
  public:
    explicit X1RoleController(std::shared_ptr<const X1RoleClient>, QObject *parent = nullptr);
    ~X1RoleController() override;
    QVariantMap state() const;
    QString deviceId() const {
        return device_;
    }
    void setDeviceId(QString);
    bool visible() const {
        return visible_;
    }
    void setVisible(bool);
    void setConnected(bool);
    void observeSnapshot(const mantis::wire::v1::Response &);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void proposeSwap();
    Q_INVOKABLE void validate();
    // Opaque confirmation identifies one validated proposal in one UI context.
    Q_INVOKABLE void apply(QString confirmation);
    Q_INVOKABLE void cancelJob();
  signals:
    void changed();
    void applied(QString parent);

  private:
    enum class Action { Query, Validate, Apply };
    struct Reply {
        mantis::wire::v1::Response snapshot;
        mantis::wire::v1::X1RoleInfo info;
        std::optional<mantis::wire::v1::ActiveCalibrationBinding> active;
        QVariantMap error;
        QString id;
        bool jobSnapshotOnly{};
    };
    struct Job {
        QString id, project, phase;
        mantis::wire::v1::X1RoleInfo before;
        quint64 epoch{};
        bool desired{};
        QVariantMap display;
    };
    std::shared_ptr<const X1RoleClient> client_;
    // Workers capture only values, this shared epoch and the client. Destruction
    // invalidates queued mutations; it never waits for network I/O on the GUI thread.
    std::shared_ptr<std::atomic<quint64>> epoch_{std::make_shared<std::atomic<quint64>>(0)};
    QFutureWatcher<Reply> request_, cancellation_;
    mantis::wire::v1::X1RoleInfo info_;
    mantis::wire::v1::Response snapshot_;
    Job job_;
    QString device_, project_, active_, confirmation_, message_;
    QVariantMap error_;
    bool connected_{}, visible_{}, fresh_{}, proposed_{}, pending_{}, cancelling_{}, cancelRequested_{};
    bool refreshAgain_{};
    void invalidate();
    QString refusal() const;
    void run(Action);
    void accept(const Reply &);
};
