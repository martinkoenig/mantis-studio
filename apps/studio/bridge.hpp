#pragma once
#include "preview.hpp"
#include <QFutureWatcher>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantList>
#include <mantis/client.hpp>
#include <mantis/render.hpp>
#include <optional>
#include <vector>
struct StudioIssue {
    std::string phase, message;
    std::optional<mantis::Error> cause;
};
struct StudioResult {
    std::optional<mantis::wire::v1::Response> snapshot;
    mantis::data::Published cloud;
    std::string cloud_id, newest_id;
    std::vector<StudioIssue> issues;
    bool operationAttempted{}, artifactAttempted{};
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
    Q_PROPERTY(QVariantList errorDetails READ errorDetails NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool capturing READ capturing NOTIFY changed)
    Q_PROPERTY(bool lastKnownCapturing READ lastKnownCapturing NOTIFY changed)
    Q_PROPERTY(QString captureStatusText READ captureStatusText NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString selectedArtifact READ selectedArtifact NOTIFY changed)
    QVariantList devices_, artifacts_, jobs_, plugins_, diagnostics_;
    QString project_, error_, capture_, selected_, newest_;
    QVariantList error_details_;
    bool connected_{};
    const bool runtime_enabled_;
    mantis::client::Client client_;
    QFutureWatcher<StudioResult> watcher_;
    QTimer timer_, preview_timer_;
    QFutureWatcher<PreviewResult> preview_watcher_;
    QPointer<MeasurementView> left_, right_;
    QString acquisition_text_, replay_;
    bool dual_preview_{}, preview_reported_{};
    void refreshPreview();
    QPointer<mantis::render::PointCloudView> view_;
    void execute(std::function<void(const mantis::client::Client &)> action = {});

  protected:
    // Internal presentation seam; asynchronous completions and deterministic tests share this path.
    void applyResult(const StudioResult &result);
    static StudioResult collectResult(const mantis::client::Client &client,
                                      const std::function<void(const mantis::client::Client &)> &action = {},
                                      std::optional<std::string> artifact = {},
                                      const std::string &displayedNewest = {});

  public:
    explicit StudioBridge(QObject *parent = nullptr, bool runtimeEnabled = true);
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
    QVariantList errorDetails() const {
        return error_details_;
    }
    bool connected() const {
        return connected_;
    }
    bool capturing() const {
        return connected_ && lastKnownCapturing();
    }
    bool lastKnownCapturing() const {
        return !capture_.isEmpty();
    }
    QString captureStatusText() const {
        if (!connected_)
            return lastKnownCapturing() ? "Last known: capture active · current state unknown"
                                        : "Current capture state unknown";
        return capturing() ? "Streaming · raw recording" : "No active capture in latest snapshot";
    }
    bool busy() const {
        return watcher_.isRunning();
    }
    QString selectedArtifact() const {
        return selected_;
    }
    QString acquisitionText() const {
        return acquisition_text_;
    }
    bool dualPreview() const {
        return dual_preview_;
    }
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
    void snapshotReady(const mantis::wire::v1::Response &);
};
