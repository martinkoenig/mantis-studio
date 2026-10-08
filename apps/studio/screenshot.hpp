#pragma once
#include <QPointer>
#include <QQuickWindow>
#include <QTimer>
#include <functional>
#include <utility>

// One bounded screenshot request. Rendering signals cross to the GUI thread before
// grabWindow; no rendering-thread readback, polling, or timing-based readiness.
class ScreenshotRequest final : public QObject {
    QPointer<QQuickWindow> window_;
    QString path_;
    QTimer request_, deadline_;
    bool requested_{}, rendered_{}, complete_{}, succeeded_{};
    std::function<void(bool, const QString &)> completion_;

    void finish(bool success, const QString &error) {
        succeeded_ = success;
        complete_ = true;
        request_.stop();
        deadline_.stop();
        completion_(success, error);
    }
    void saveIfReady() {
        if (complete_ || !requested_ || !rendered_)
            return;
        // grabWindow may cause another render signal; this request must stay single-shot.
        complete_ = true;
        if (!window_) {
            finish(false, "Screenshot window was destroyed");
            return;
        }
        const auto image = window_->grabWindow();
        if (image.isNull())
            finish(false, "Screenshot readback returned an empty image");
        else if (!image.save(path_, "PNG"))
            finish(false, "Screenshot PNG save failed: " + path_);
        else
            finish(true, {});
    }

  public:
    ScreenshotRequest(QQuickWindow *window, QString path, int delayMs, int timeoutMs,
                      std::function<void(bool, const QString &)> completion, QObject *parent = nullptr)
        : QObject(parent), window_(window), path_(std::move(path)), completion_(std::move(completion)) {
        request_.setSingleShot(true);
        deadline_.setSingleShot(true);
        connect(&request_, &QTimer::timeout, this, [this] {
            requested_ = true;
            saveIfReady();
            if (!complete_ && window_)
                window_->update();
        });
        connect(&deadline_, &QTimer::timeout, this, [this] {
            if (!complete_)
                finish(false, "Screenshot deadline expired before a rendered frame");
        });
        if (!window_) {
            // Report invalid-window failure from the event loop like other outcomes.
            QTimer::singleShot(0, this, [this] { finish(false, "Screenshot window unavailable"); });
            return;
        }
        connect(
            window, &QQuickWindow::afterFrameEnd, this,
            [this] {
                rendered_ = true;
                saveIfReady();
            },
            Qt::QueuedConnection);
        connect(
            window, &QQuickWindow::sceneGraphError, this,
            [this](QQuickWindow::SceneGraphError, const QString &error) {
                if (!complete_)
                    finish(false, "Screenshot scene graph error: " + error);
            },
            Qt::QueuedConnection);
        connect(window, &QObject::destroyed, this, [this] {
            if (!complete_)
                finish(false, "Screenshot window was destroyed");
        });
        request_.start(delayMs);
        deadline_.start(timeoutMs);
        window->update();
    }
    bool succeeded() const {
        return succeeded_;
    }
};
