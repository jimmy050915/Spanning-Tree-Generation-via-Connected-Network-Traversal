#include "presentation/graphics/TraversalTreeView.h"

#include "presentation/graphics/TraversalTreeSceneController.h"

#include <QGraphicsScene>
#include <QPainter>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace novel::presentation {

TraversalTreeView::TraversalTreeView(QWidget* parent)
    : QGraphicsView(parent),
      ownedScene_(new QGraphicsScene(this)),
      controller_(new TraversalTreeSceneController(ownedScene_, this)) {
    setScene(ownedScene_);
    setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::AnchorViewCenter);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setMinimumSize(280, 180);
    setAccessibleName(tr("遍历树"));

    connect(controller_, &TraversalTreeSceneController::personSelected,
            this, &TraversalTreeView::personSelected);
    connect(controller_, &TraversalTreeSceneController::selectionCleared,
            this, &TraversalTreeView::selectionCleared);
}

TraversalTreeView::~TraversalTreeView() {
    setScene(nullptr);
    delete controller_;
    controller_ = nullptr;
    delete ownedScene_;
    ownedScene_ = nullptr;
}

void TraversalTreeView::setTraversal(
    const application::TraversalResultDto& traversal) {
    controller_->setTraversal(traversal);
    resetView();
}

void TraversalTreeView::clear() {
    controller_->clear();
    resetTransform();
    viewport()->update();
}

bool TraversalTreeView::focusPerson(PersonId personId) {
    if (!controller_->focusPerson(personId)) {
        return false;
    }
    const auto position = controller_->nodePosition(personId);
    centerOn(position);
    return true;
}

void TraversalTreeView::resetView() {
    if (fittingView_) {
        return;
    }
    QScopedValueRollback guard(fittingView_, true);
    resetTransform();
    if (scene() == nullptr || scene()->items().isEmpty()) {
        return;
    }
    const auto target = scene()->itemsBoundingRect().adjusted(-30.0, -30.0, 30.0, 30.0);
    fitInView(target, Qt::KeepAspectRatio);
    if (transform().m11() > maximumScale_) {
        resetTransform();
        scale(maximumScale_, maximumScale_);
    }
}

TraversalTreeSceneController* TraversalTreeView::sceneController() const noexcept {
    return controller_;
}

void TraversalTreeView::wheelEvent(QWheelEvent* event) {
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

void TraversalTreeView::resizeEvent(QResizeEvent* event) {
    QGraphicsView::resizeEvent(event);
    resetView();
}

void TraversalTreeView::drawBackground(QPainter* painter, const QRectF& rect) {
    QGraphicsView::drawBackground(painter, rect);
    if (controller_->nodeCount() != 0) {
        return;
    }
    painter->save();
    painter->setPen(palette().placeholderText().color());
    painter->drawText(sceneRect(), Qt::AlignCenter, tr("尚未运行遍历"));
    painter->restore();
}

}  // namespace novel::presentation
