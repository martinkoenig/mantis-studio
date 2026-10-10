#pragma once
#include "calibration_client.hpp"
#include <QFutureWatcher>
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <map>

class CalibrationController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    Q_PROPERTY(QVariantList captures READ captures NOTIFY changed)
    Q_PROPERTY(QVariantList artifacts READ artifacts NOTIFY changed)
    Q_PROPERTY(QStringList stages READ stages CONSTANT)
    Q_PROPERTY(int stage READ stage WRITE setStage NOTIFY changed)
    Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY changed)
  public:
    explicit CalibrationController(QObject *parent = nullptr);
    explicit CalibrationController(std::shared_ptr<const CalibrationClient>, QObject *parent = nullptr);
    ~CalibrationController() override;
    QVariantMap state() const;
    QVariantList devices() const;
    QVariantList captures() const;
    QVariantList artifacts() const;
    QStringList stages() const {
        return {"Device", "Target", "Captures", "Dataset", "Cameras", "Rig", "Review & Activate"};
    }
    int stage() const {
        return stage_;
    }
    void setStage(int);
    bool visible() const {
        return visible_;
    }
    void setVisible(bool);
    // Presence is expressed by explicit keys, independently of UI expansion.
    static mantis::wire::v1::CalibrationTargetSpecification targetSpecification(const QVariantMap &);
    // Shared read-only eligibility; device presentation must not invent another rule.
    static bool isCalibrationCandidate(const mantis::wire::v1::Device &, const mantis::wire::v1::Response &);
    void observeSnapshot(const mantis::wire::v1::Response &);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void selectDevice(QString);
    Q_INVOKABLE void selectArtifact(QString slot, QString id);
    Q_INVOKABLE QVariantList choices(QString slot) const;
    Q_INVOKABLE void setCaptureSelected(QString id, bool selected);
    Q_INVOKABLE void createTarget(QVariantMap form);
    Q_INVOKABLE void startCapture();
    Q_INVOKABLE void stopCapture();
    Q_INVOKABLE void buildDataset(int maxSelected);
    Q_INVOKABLE void solveCamera(QString role, int heldout);
    Q_INVOKABLE void solveBoth(int heldout);
    Q_INVOKABLE void solveRig(int heldout, QString frameId, QString frameName);
    // Confirmation carries both exact IDs; stale confirmation never changes another binding.
    Q_INVOKABLE void activate(QString confirmedDevice, QString confirmedRig);
    Q_INVOKABLE void clearActive(QString confirmedDevice);
    Q_INVOKABLE void cancelJob(QString slot);
  signals:
    void changed();

  private:
    struct Reply {
        std::vector<mantis::wire::v1::CalibrationEntry> entries;
        QMap<QString, mantis::wire::v1::CalibrationInfo> infos;
        mantis::wire::v1::Response snapshot;
        std::optional<mantis::wire::v1::ActiveCalibrationBinding> active;
        QString id, device;
        QVariantMap error, inspectionErrors;
    };
    struct TrackedJob {
        QString id, input, left, right;
        QVariantMap display;
        bool bound{};
    };
    std::shared_ptr<const CalibrationClient> client_;
    QFutureWatcher<Reply> refresh_;
    std::map<QString, std::unique_ptr<QFutureWatcher<Reply>>> requests_;
    QTimer timer_;
    QMap<QString, mantis::wire::v1::CalibrationInfo> infos_;
    std::vector<mantis::wire::v1::CalibrationEntry> entries_;
    mantis::wire::v1::Response snapshot_;
    QMap<QString, QString> selected_;
    QStringList raw_;
    QString device_, capture_;
    QVariantMap active_, errors_, inspectionErrors_;
    QMap<QString, TrackedJob> jobs_;
    int stage_{};
    bool visible_{}, refreshAgain_{};
    void request(const QString &, std::function<Reply(const CalibrationClient &)>,
                 std::function<void(const Reply &)>);
    bool pending(const QString &) const;
    bool running(const QString &) const;
    void error(const QString &, const mantis::Error &);
    void bindResults();
    void select(const QString &, const QString &);
    QVariantMap evidence(const QString &) const;
};
