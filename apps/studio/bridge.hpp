#pragma once
#include <QFutureWatcher>
#include "preview.hpp"
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <mantis/client.hpp>
#include <mantis/render.hpp>
struct StudioResult {
    mantis::wire::v1::Response snapshot;
    mantis::data::Published cloud;
    std::string cloud_id, newest_id, error;
};
class StudioBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString acquisitionText READ acquisitionText NOTIFY changed)
    Q_PROPERTY(bool dualPreview READ dualPreview NOTIFY changed)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY changed)
    Q_PROPERTY(QVariantList artifacts READ artifacts NOTIFY changed)
    Q_PROPERTY(QVariantList jobs READ jobs NOTIFY changed)
    Q_PROPERTY(QVariantList plugins READ plugins NOTIFY changed)
    Q_PROPERTY(QVariantList diagnostics READ diagnostics NOTIFY changed)
    Q_PROPERTY(QString project READ project NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool capturing READ capturing NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString selectedArtifact READ selectedArtifact NOTIFY changed)
    QVariantList devices_, artifacts_, jobs_, plugins_, diagnostics_;
    QString project_, error_, capture_, selected_, newest_;
    bool connected_{};
    mantis::client::Client client_;
    QFutureWatcher<StudioResult> watcher_;
    QTimer timer_, preview_timer_;
    QFutureWatcher<PreviewResult> preview_watcher_;
    MeasurementView *left_{}, *right_{};
    QString acquisition_text_, replay_;
    bool dual_preview_{}, preview_reported_{};
    void refreshPreview();
    mantis::render::PointCloudView *view_{};
    void execute(std::function<void(const mantis::client::Client &)> action = {});

  public:
    explicit StudioBridge(QObject *parent = nullptr);
    ~StudioBridge() override;
    QVariantList devices() const {
        return devices_;
    }
    QVariantList artifacts() const {
        return artifacts_;
    }
    QVariantList jobs() const {
        return jobs_;
    }
    QVariantList plugins() const {
        return plugins_;
    }
    QVariantList diagnostics() const {
        return diagnostics_;
    }
    QString project() const {
        return project_;
    }
    QString error() const {
        return error_;
    }
    bool connected() const {
        return connected_;
    }
    bool capturing() const {
        return !capture_.isEmpty();
    }
    bool busy() const {
        return watcher_.isRunning();
    }
    QString selectedArtifact() const {
        return selected_;
    }
    QString acquisitionText() const { return acquisition_text_; }
    bool dualPreview() const { return dual_preview_; }
    Q_INVOKABLE void attachPreview(QObject *left, QObject *right);
    Q_INVOKABLE void replay(QString artifact, bool verify);
    Q_INVOKABLE void attachView(QObject *);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void startCapture(QString device);
    Q_INVOKABLE void stopCapture();
    Q_INVOKABLE void runPipeline(QString recipe);
    Q_INVOKABLE void exportArtifact(QString path);
    Q_INVOKABLE void cancelJob(QString id);
    Q_INVOKABLE void selectArtifact(QString id);
    Q_INVOKABLE void enablePlugin(QString id, bool enabled);
    bool viewportReady() const {
        return view_ && view_->width() > 100 && view_->height() > 100 && view_->pointCount() > 0;
    }
  signals:
    void changed();
};
