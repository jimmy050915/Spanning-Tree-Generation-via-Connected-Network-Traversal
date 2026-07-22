#include "presentation/graphics/RelationGraphView.h"

#include "presentation/graphics/PersonNodeItem.h"
#include "presentation/graphics/RelationGraphSceneController.h"

#include <QGraphicsScene>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace novel::presentation {

RelationGraphView::RelationGraphView(QWidget* parent)
    : QGraphicsView(parent),
      ownedScene_(new QGraphicsScene(this)),
      controller_(new RelationGraphSceneController(ownedScene_, this)) {
    setScene(ownedScene_);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing |
                   QPainter::SmoothPixmapTransform);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setViewportUpdateMode(QGraphicsView::BoundingRectViewportUpdate);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setMinimumSize(280, 220);
    setAccessibleName(tr("人物关系图"));

    animationTimer_.setSingleShot(false);
    connect(&animationTimer_, &QTimer::timeout,
            this, &RelationGraphView::advanceTraversal);
    connect(controller_, &RelationGraphSceneController::personSelected,
            this, &RelationGraphView::personSelected);
    connect(controller_, &RelationGraphSceneController::relationSelected,
            this, &RelationGraphView::relationSelected);
    connect(controller_, &RelationGraphSceneController::selectionCleared,
            this, &RelationGraphView::selectionCleared);
}

RelationGraphView::~RelationGraphView() {
    animationTimer_.stop();
    setScene(nullptr);
    delete controller_;
    controller_ = nullptr;
    delete ownedScene_;
    ownedScene_ = nullptr;
}

void RelationGraphView::setSnapshot(const application::GraphSnapshotDto& snapshot) {
    cancelTraversal(false);
    controller_->setSnapshot(snapshot);
    resetView();
}

void RelationGraphView::clear() {
    cancelTraversal(false);
    controller_->clear();
    resetTransform();
    viewport()->update();
}

bool RelationGraphView::focusPerson(PersonId personId) {
    if (!controller_->focusPerson(personId)) {
        return false;
    }
    centerOn(controller_->nodeItem(personId));
    return true;
}

bool RelationGraphView::focusPerson(const QString& canonicalName) {
    if (!controller_->focusPerson(canonicalName)) {
        return false;
    }
    const auto selected = scene()->selectedItems();
    if (!selected.isEmpty()) {
        centerOn(selected.front());
    }
    return true;
}

void RelationGraphView::setTraversalHighlight(PersonId personId,
                                              PersonId previousPersonId) {
    controller_->setTraversalHighlight(personId, previousPersonId);
    if (auto* node = controller_->nodeItem(personId); node != nullptr) {
        centerOn(node);
    }
}

void RelationGraphView::clearTraversalHighlight() {
    controller_->clearTraversalHighlight();
}

void RelationGraphView::playTraversal(
    const application::TraversalResultDto& traversal,
    int intervalMilliseconds) {
    playTraversalOrder(traversal.order, intervalMilliseconds);
}

void RelationGraphView::playTraversalOrder(const std::vector<PersonId>& order,
                                           int intervalMilliseconds) {
    cancelTraversal(false);
    traversalOrder_.reserve(order.size());
    for (const auto person : order) {
        if (controller_->nodeItem(person) != nullptr) {
            traversalOrder_.push_back(person);
        }
    }
    nextTraversalIndex_ = 0;
    if (traversalOrder_.empty()) {
        controller_->clearTraversalHighlight();
        emit traversalFinished();
        return;
    }

    animationTimer_.setInterval(std::max(1, intervalMilliseconds));
    advanceTraversal();
    // Keep the final node visible for one complete interval. The following
    // timeout performs natural completion and restores normal interaction.
    animationTimer_.start();
}

void RelationGraphView::stopTraversal() {
    cancelTraversal(true);
}

void RelationGraphView::resetView() {
    if (fittingView_) {
        return;
    }
    QScopedValueRollback guard(fittingView_, true);
    resetTransform();
    if (scene() == nullptr || scene()->items().isEmpty()) {
        return;
    }
    const auto target = scene()->itemsBoundingRect().adjusted(-45.0, -45.0, 45.0, 45.0);
    fitInView(target, Qt::KeepAspectRatio);
    if (transform().m11() > maximumScale_) {
        resetTransform();
        scale(maximumScale_, maximumScale_);
    }
}

bool RelationGraphView::isTraversalPlaying() const noexcept {
    return animationTimer_.isActive();
}

RelationGraphSceneController* RelationGraphView::sceneController() const noexcept {
    return controller_;
}

void RelationGraphView::wheelEvent(QWheelEvent* event) {
    if (event->angleDelta().y() == 0) {
        QGraphicsView::wheelEvent(event);
        return;
    }

    const auto currentScale = transform().m11();
    const auto requestedFactor = std::pow(1.0015, event->angleDelta().y());
    const auto targetScale = std::clamp(currentScale * requestedFactor,
                                        minimumScale_, maximumScale_);
    const auto factor = targetScale / currentScale;
    if (std::abs(factor - 1.0) <= 0.00001) {
        event->accept();
        return;
    }

    const auto viewportPosition = event->position();
    const auto before = mapToScene(viewportPosition.toPoint());
    scale(factor, factor);
    const auto after = mapToScene(viewportPosition.toPoint());
    translate(after.x() - before.x(), after.y() - before.y());
    event->accept();
}

void RelationGraphView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        middleButtonPanning_ = true;
        lastPanPosition_ = event->position().toPoint();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void RelationGraphView::mouseMoveEvent(QMouseEvent* event) {
    if (middleButtonPanning_) {
        const auto current = event->position().toPoint();
        const auto delta = current - lastPanPosition_;
        lastPanPosition_ = current;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void RelationGraphView::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton && middleButtonPanning_) {
        middleButtonPanning_ = false;
        unsetCursor();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void RelationGraphView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    resetView();
}

void RelationGraphView::drawBackground(QPainter* painter, const QRectF& rect) {
    QGraphicsView::drawBackground(painter, rect);
    if (controller_->nodeCount() != 0) {
        return;
    }
    painter->save();
    painter->setPen(palette().placeholderText().color());
    painter->drawText(sceneRect(), Qt::AlignCenter, tr("暂无关系数据"));
    painter->restore();
}

void RelationGraphView::advanceTraversal() {
    if (nextTraversalIndex_ >= traversalOrder_.size()) {
        animationTimer_.stop();
        traversalOrder_.clear();
        nextTraversalIndex_ = 0;
        controller_->clearTraversalHighlight();
        emit traversalFinished();
        return;
    }

    const auto currentIndex = nextTraversalIndex_;
    const auto person = traversalOrder_[currentIndex];
    const auto previous = currentIndex == 0U
                              ? PersonId{}
                              : traversalOrder_[currentIndex - 1U];
    controller_->setTraversalHighlight(person, previous);
    if (auto* node = controller_->nodeItem(person); node != nullptr) {
        centerOn(node);
    }
    emit traversalStep(person, static_cast<int>(currentIndex));
    ++nextTraversalIndex_;

}

void RelationGraphView::cancelTraversal(bool notify) {
    const bool hadTraversal = animationTimer_.isActive() || !traversalOrder_.empty();
    animationTimer_.stop();
    traversalOrder_.clear();
    nextTraversalIndex_ = 0;
    controller_->clearTraversalHighlight();
    if (notify && hadTraversal) {
        emit traversalStopped();
    }
}

}  // namespace novel::presentation
