#include "presentation/graphics/PersonNodeItem.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QGraphicsSceneHoverEvent>
#include <QGraphicsSceneMouseEvent>
#include <QLineF>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPen>
#include <QStyleOptionGraphicsItem>

#include <algorithm>
#include <utility>

namespace novel::presentation {

namespace {

QColor blended(const QColor& first, const QColor& second, qreal amount) {
    const auto bounded = std::clamp(amount, 0.0, 1.0);
    return QColor::fromRgbF(first.redF() * (1.0 - bounded) + second.redF() * bounded,
                            first.greenF() * (1.0 - bounded) + second.greenF() * bounded,
                            first.blueF() * (1.0 - bounded) + second.blueF() * bounded,
                            first.alphaF() * (1.0 - bounded) + second.alphaF() * bounded);
}

}  // namespace

PersonNodeItem::PersonNodeItem(PersonId id,
                               QString canonicalName,
                               std::uint32_t chapterCount,
                               qreal radius,
                               QGraphicsItem* parent)
    : QGraphicsObject(parent),
      personId_(id),
      canonicalName_(std::move(canonicalName)),
      chapterCount_(chapterCount),
      radius_(std::max<qreal>(radius, 12.0)) {
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setAcceptHoverEvents(true);
    setCacheMode(DeviceCoordinateCache);
    setCursor(Qt::OpenHandCursor);
    setToolTip(tr("人物：%1\n出现章节：%2")
                   .arg(canonicalName_)
                   .arg(chapterCount_));
}

int PersonNodeItem::type() const {
    return Type;
}

QRectF PersonNodeItem::boundingRect() const {
    constexpr qreal outlineAllowance = 4.0;
    const auto extent = radius_ + outlineAllowance;
    return {-extent, -extent, extent * 2.0, extent * 2.0};
}

QPainterPath PersonNodeItem::shape() const {
    QPainterPath path;
    path.addEllipse(QPointF{}, radius_, radius_);
    return path;
}

void PersonNodeItem::paint(QPainter* painter,
                           const QStyleOptionGraphicsItem*,
                           QWidget*) {
    const auto palette = QApplication::palette();
    QColor fill = palette.button().color();
    QColor outline = palette.mid().color();
    QColor text = palette.buttonText().color();
    qreal opacity = 1.0;
    qreal penWidth = 1.5;

    switch (visualState_) {
    case VisualState::Normal:
        fill = blended(palette.base().color(), palette.highlight().color(), 0.35);
        break;
    case VisualState::Dimmed:
        fill = palette.alternateBase().color();
        opacity = 0.28;
        break;
    case VisualState::Adjacent:
        fill = blended(palette.base().color(), palette.highlight().color(), 0.58);
        outline = palette.highlight().color();
        penWidth = 2.2;
        break;
    case VisualState::Selected:
        fill = palette.highlight().color();
        outline = palette.highlight().color().darker(130);
        text = palette.highlightedText().color();
        penWidth = 3.0;
        break;
    case VisualState::Traversal:
        fill = QColor(240, 153, 54);
        outline = fill.darker(145);
        text = QColor(Qt::black);
        penWidth = 3.2;
        break;
    }

    if (hovered_ && visualState_ != VisualState::Dimmed) {
        fill = fill.lighter(112);
        penWidth += 0.6;
    }

    painter->save();
    painter->setOpacity(opacity);
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(outline, penWidth));
    painter->setBrush(fill);
    painter->drawEllipse(QPointF{}, radius_, radius_);

    QFont labelFont = painter->font();
    labelFont.setBold(visualState_ == VisualState::Selected ||
                      visualState_ == VisualState::Traversal);
    labelFont.setPointSizeF(std::max(8.0, std::min(11.0, radius_ / 2.8)));
    painter->setFont(labelFont);
    painter->setPen(text);

    const QFontMetricsF metrics(labelFont);
    const auto label = metrics.elidedText(canonicalName_, Qt::ElideRight,
                                          std::max<qreal>(24.0, radius_ * 1.55));
    painter->drawText(QRectF{-radius_ * 0.82, -radius_ * 0.5,
                             radius_ * 1.64, radius_},
                      Qt::AlignCenter, label);
    painter->restore();
}

PersonId PersonNodeItem::personId() const noexcept {
    return personId_;
}

const QString& PersonNodeItem::canonicalName() const noexcept {
    return canonicalName_;
}

std::uint32_t PersonNodeItem::chapterCount() const noexcept {
    return chapterCount_;
}

qreal PersonNodeItem::radius() const noexcept {
    return radius_;
}

PersonNodeItem::VisualState PersonNodeItem::visualState() const noexcept {
    return visualState_;
}

void PersonNodeItem::setVisualState(VisualState state) {
    if (visualState_ == state) {
        return;
    }
    visualState_ = state;
    update();
}

QVariant PersonNodeItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    const auto result = QGraphicsObject::itemChange(change, value);
    if (change == ItemPositionHasChanged) {
        emit positionChanged(personId_, scenePos());
    }
    return result;
}

void PersonNodeItem::hoverEnterEvent(QGraphicsSceneHoverEvent* event) {
    hovered_ = true;
    setCursor(Qt::OpenHandCursor);
    update();
    QGraphicsObject::hoverEnterEvent(event);
}

void PersonNodeItem::hoverLeaveEvent(QGraphicsSceneHoverEvent* event) {
    hovered_ = false;
    update();
    QGraphicsObject::hoverLeaveEvent(event);
}

void PersonNodeItem::mousePressEvent(QGraphicsSceneMouseEvent* event) {
    selectedWhenPressed_ = isSelected();
    pressScenePosition_ = event->scenePos();
    setCursor(Qt::ClosedHandCursor);
    QGraphicsObject::mousePressEvent(event);
}

void PersonNodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent* event) {
    setCursor(Qt::OpenHandCursor);
    QGraphicsObject::mouseReleaseEvent(event);
    // A newly selected item is reported by QGraphicsScene::selectionChanged.
    // Emit only for a genuine re-click so detail panes can be restored without
    // producing two notifications for the first click.
    if (event->button() == Qt::LeftButton && selectedWhenPressed_ && isSelected() &&
        QLineF(pressScenePosition_, event->scenePos()).length() <= 4.0) {
        emit activated(personId_);
    }
}

}  // namespace novel::presentation
