#include "presentation/graphics/RelationEdgeItem.h"

#include "presentation/graphics/PersonNodeItem.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <QPalette>
#include <QPen>

#include <algorithm>
#include <cmath>
#include <utility>

namespace novel::presentation {

RelationEdgeItem::RelationEdgeItem(EdgeId edgeId,
                                   PersonNodeItem* endpointA,
                                   PersonNodeItem* endpointB,
                                   QString endpointAName,
                                   QString endpointBName,
                                   std::uint32_t coChapterCount,
                                   double jaccard,
                                   QGraphicsItem* parent)
    : QGraphicsObject(parent),
      edgeId_(edgeId),
      endpointAItem_(endpointA),
      endpointBItem_(endpointB),
      endpointAName_(std::move(endpointAName)),
      endpointBName_(std::move(endpointBName)),
      coChapterCount_(coChapterCount),
      jaccard_(std::clamp(jaccard, 0.0, 1.0)) {
    setFlag(ItemIsSelectable, true);
    setAcceptHoverEvents(true);
    setCursor(Qt::PointingHandCursor);
    setZValue(-1.0);
    setToolTip(tr("%1 — %2\n共同章节：%3\nJaccard：%4")
                   .arg(endpointAName_, endpointBName_)
                   .arg(coChapterCount_)
                   .arg(jaccard_, 0, 'f', 3));
    adjust();
}

int RelationEdgeItem::type() const {
    return Type;
}

QRectF RelationEdgeItem::boundingRect() const {
    if (line_.isNull()) {
        return {};
    }
    constexpr qreal allowance = 10.0;
    auto result = QRectF(line_.p1(), line_.p2())
                      .normalized()
                      .adjusted(-allowance, -allowance, allowance, allowance);
    const auto midpoint = line_.pointAt(0.5);
    const QRectF maximumLabelBounds{midpoint.x() - 120.0,
                                    midpoint.y() - 28.0,
                                    240.0,
                                    56.0};
    return result.united(maximumLabelBounds);
}

QPainterPath RelationEdgeItem::shape() const {
    QPainterPath path;
    path.moveTo(line_.p1());
    path.lineTo(line_.p2());
    QPainterPathStroker stroker;
    stroker.setWidth(std::max<qreal>(10.0, effectiveWidth() + 6.0));
    return stroker.createStroke(path);
}

void RelationEdgeItem::paint(QPainter* painter,
                             const QStyleOptionGraphicsItem*,
                             QWidget*) {
    if (line_.isNull()) {
        return;
    }

    const auto palette = QApplication::palette();
    QColor color = palette.mid().color();
    qreal opacity = 0.82;

    switch (visualState_) {
    case VisualState::Normal:
        break;
    case VisualState::Dimmed:
        opacity = 0.12;
        break;
    case VisualState::Adjacent:
        color = palette.highlight().color();
        opacity = 0.9;
        break;
    case VisualState::Selected:
        color = palette.highlight().color().darker(120);
        opacity = 1.0;
        break;
    case VisualState::Traversal:
        color = QColor(240, 153, 54);
        opacity = 1.0;
        break;
    }

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setOpacity(opacity);
    painter->setPen(QPen(color, effectiveWidth(), Qt::SolidLine, Qt::RoundCap));
    painter->drawLine(line_);

    if (hovered_ || isUnderMouse() || isSelected()) {
        const auto label = tr("共同 %1 章  ·  Jaccard %2")
                               .arg(coChapterCount_)
                               .arg(jaccard_, 0, 'f', 3);
        const auto midpoint = line_.pointAt(0.5);
        const QFontMetricsF metrics(painter->font());
        QRectF labelRect = metrics.boundingRect(label).adjusted(-7.0, -4.0, 7.0, 4.0);
        labelRect.moveCenter(midpoint);
        painter->setOpacity(0.94);
        painter->setPen(QPen(palette.mid().color(), 0.8));
        painter->setBrush(palette.toolTipBase());
        painter->drawRoundedRect(labelRect, 4.0, 4.0);
        painter->setPen(palette.toolTipText().color());
        painter->drawText(labelRect, Qt::AlignCenter, label);
    }
    painter->restore();
}

EdgeId RelationEdgeItem::edgeId() const noexcept {
    return edgeId_;
}

PersonId RelationEdgeItem::endpointA() const noexcept {
    return endpointAItem_ == nullptr ? PersonId{} : endpointAItem_->personId();
}

PersonId RelationEdgeItem::endpointB() const noexcept {
    return endpointBItem_ == nullptr ? PersonId{} : endpointBItem_->personId();
}

std::uint32_t RelationEdgeItem::coChapterCount() const noexcept {
    return coChapterCount_;
}

double RelationEdgeItem::jaccard() const noexcept {
    return jaccard_;
}

qreal RelationEdgeItem::normalWidth() const noexcept {
    return 1.2 + static_cast<qreal>(jaccard_) * 5.0;
}

RelationEdgeItem::VisualState RelationEdgeItem::visualState() const noexcept {
    return visualState_;
}

bool RelationEdgeItem::connects(PersonId person) const noexcept {
    return endpointA() == person || endpointB() == person;
}

bool RelationEdgeItem::connects(PersonId first, PersonId second) const noexcept {
    return (endpointA() == first && endpointB() == second) ||
           (endpointA() == second && endpointB() == first);
}

void RelationEdgeItem::setVisualState(VisualState state) {
    if (visualState_ == state) {
        return;
    }
    prepareGeometryChange();
    visualState_ = state;
    update();
}

void RelationEdgeItem::adjust() {
    if (endpointAItem_ == nullptr || endpointBItem_ == nullptr) {
        return;
    }

    prepareGeometryChange();
    const QLineF centerLine(endpointAItem_->pos(), endpointBItem_->pos());
    if (centerLine.length() <= 0.001) {
        line_ = {};
        return;
    }

    const QPointF unit = (centerLine.p2() - centerLine.p1()) / centerLine.length();
    const auto start = centerLine.p1() + unit * endpointAItem_->radius();
    const auto end = centerLine.p2() - unit * endpointBItem_->radius();
    line_ = QLineF(start, end);
    update();
}

void RelationEdgeItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event) {
    prepareGeometryChange();
    hovered_ = true;
    update();
    QGraphicsObject::hoverEnterEvent(event);
}

void RelationEdgeItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
    prepareGeometryChange();
    hovered_ = false;
    update();
    QGraphicsObject::hoverLeaveEvent(event);
}

void RelationEdgeItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    selectedWhenPressed_ = isSelected();
    QGraphicsObject::mousePressEvent(event);
}

void RelationEdgeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    QGraphicsObject::mouseReleaseEvent(event);
    if (event->button() == Qt::LeftButton && selectedWhenPressed_ && isSelected()) {
        emit activated(edgeId_, endpointA(), endpointB());
    }
}

qreal RelationEdgeItem::effectiveWidth() const noexcept {
    auto width = normalWidth();
    if (visualState_ == VisualState::Selected ||
        visualState_ == VisualState::Traversal) {
        width += 2.0;
    } else if (visualState_ == VisualState::Adjacent || hovered_) {
        width += 1.0;
    }
    return width;
}

}  // namespace novel::presentation
