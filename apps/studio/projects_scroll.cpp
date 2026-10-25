#include "projects_scroll.hpp"
#include <QEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

void ProjectsScrollInput::setViewport(QQuickItem *viewport) {
    if (viewport_ == viewport)
        return;
    if (viewport_)
        viewport_->removeEventFilter(this);
    cancelPending();
    viewport_ = viewport;
    if (viewport_)
        viewport_->installEventFilter(this);
    emit viewportChanged();
}
void ProjectsScrollInput::cancelPending() {
    gesture_ = momentum_ = pending_ = false;
    previousTime_ = lastUpdate_ = 0;
    samples_ = 0;
    releaseVelocity_ = 0;
}
bool ProjectsScrollInput::eventFilter(QObject *object, QEvent *event) {
    if (object != viewport_)
        return false;
    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::TouchBegin ||
        event->type() == QEvent::KeyPress) {
        cancelPending();
        return false;
    }
    if (event->type() != QEvent::Wheel)
        return false;
    auto *wheel = static_cast<QWheelEvent *>(event);
    const auto timestamp = static_cast<quint64>(wheel->timestamp());
    switch (wheel->phase()) {
    case Qt::ScrollBegin:
        cancelPending();
        gesture_ = true;
        previousTime_ = timestamp;
        break;
    case Qt::ScrollMomentum:
        // A platform-supplied momentum phase owns the entire release.
        momentum_ = true;
        pending_ = false;
        break;
    case Qt::ScrollUpdate:
        if (gesture_ && !momentum_ && !wheel->pixelDelta().isNull() && timestamp > previousTime_) {
            const qreal delta = wheel->pixelDelta().y();
            const auto interval = timestamp - previousTime_;
            // Same 100ms release/pause cutoff as Qt 6.4 Flickable's native drag release.
            if (!delta || interval >= 100 ||
                (samples_ &&
                 std::signbit(delta) != std::signbit(velocities_[static_cast<size_t>(samples_ - 1)])))
                samples_ = 0;
            if (delta && interval < 100) {
                if (samples_ == 4) {
                    for (size_t i = 0; i < 3; ++i)
                        velocities_[i] = velocities_[i + 1];
                    --samples_;
                }
                velocities_[static_cast<size_t>(samples_++)] = delta * 1000 / static_cast<qreal>(interval);
                lastUpdate_ = timestamp;
            }
        }
        previousTime_ = timestamp;
        break;
    case Qt::ScrollEnd:
        if (gesture_ && !momentum_ && samples_ >= 2 && timestamp >= lastUpdate_ &&
            timestamp - lastUpdate_ < 100) {
            const int count = std::min(samples_, 4);
            releaseVelocity_ = 0;
            for (int i = 0; i < count; ++i)
                releaseVelocity_ += velocities_[static_cast<size_t>(i)];
            releaseVelocity_ /= count;
            pending_ = true;
            // Let Flickable finish ScrollEnd first, then use its public native flick.
            QMetaObject::invokeMethod(this, &ProjectsScrollInput::finishGesture, Qt::QueuedConnection);
        }
        gesture_ = false;
        break;
    case Qt::NoScrollPhase:
        cancelPending(); // Ordinary wheel and unphased input retain Qt's behavior.
        break;
    }
    if ((wheel->phase() == Qt::ScrollUpdate || wheel->phase() == Qt::ScrollMomentum) &&
        !wheel->pixelDelta().isNull() && viewport_->isVisible() && viewport_->isEnabled() &&
        viewport_->property("interactive").toBool()) {
        const auto limit =
            std::max(0.0, viewport_->property("contentHeight").toDouble() - viewport_->height());
        if (limit > 0) {
            // Native pixel input is already direction-adjusted by the platform.
            // Apply it immediately: Flickable's wheel-as-drag path otherwise drops
            // the first movement while acquiring its drag. OS momentum uses the
            // very same pixel path, without an additional release animation.
            const auto y = viewport_->property("contentY").toDouble();
            QMetaObject::invokeMethod(viewport_, "cancelFlick");
            viewport_->setProperty("contentY", std::clamp(y - wheel->pixelDelta().y(), 0.0, limit));
            wheel->accept();
            return true;
        }
    }
    return false; // Mouse wheel, touch, and phase boundaries stay Qt-owned.
}
void ProjectsScrollInput::finishGesture() {
    if (!pending_ || !viewport_)
        return;
    pending_ = false;
    auto *view = viewport_.data();
    if (!view->isVisible() || !view->isEnabled() || !view->property("interactive").toBool() || momentum_)
        return;
    const auto y = view->property("contentY").toDouble();
    const auto limit = std::max(0.0, view->property("contentHeight").toDouble() - view->height());
    const auto maximum = view->property("maximumFlickVelocity").toDouble();
    const qreal velocity = maximum > 0 ? std::clamp(releaseVelocity_, -maximum, maximum) : releaseVelocity_;
    if (!velocity || limit <= 0 || (velocity < 0 && y >= limit) || (velocity > 0 && y <= 0))
        return;
    if (QMetaObject::invokeMethod(view, "flick", Q_ARG(qreal, 0), Q_ARG(qreal, velocity)))
        emit nativeCoastStarted(velocity);
}
