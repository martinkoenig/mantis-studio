#pragma once
#include <QPointer>
#include <QQuickItem>
#include <array>

// Presentation-only adapter for phased pixel gestures without OS momentum.
// Precise phased pixels move immediately; Qt owns the native release trajectory.
class ProjectsScrollInput : public QObject {
    Q_OBJECT
    Q_PROPERTY(QQuickItem *viewport READ viewport WRITE setViewport NOTIFY viewportChanged)
  public:
    explicit ProjectsScrollInput(QObject *parent = nullptr) : QObject(parent) {}
    QQuickItem *viewport() const {
        return viewport_;
    }
    void setViewport(QQuickItem *viewport);
    Q_INVOKABLE void cancelPending();
  signals:
    void viewportChanged();
    void nativeCoastStarted(qreal velocity);

  protected:
    bool eventFilter(QObject *object, QEvent *event) override;

  private:
    void finishGesture();
    QPointer<QQuickItem> viewport_;
    std::array<qreal, 4> velocities_{};
    int samples_ = 0;
    quint64 previousTime_ = 0, lastUpdate_ = 0;
    qreal releaseVelocity_ = 0;
    bool gesture_ = false, momentum_ = false, pending_ = false;
};
