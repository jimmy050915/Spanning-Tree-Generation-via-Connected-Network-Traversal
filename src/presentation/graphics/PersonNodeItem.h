#pragma once

#include "domain/model/GraphTypes.h"

#include <QGraphicsObject>
#include <QString>

class QGraphicsSceneMouseEvent;
class QPainter;
class QPainterPath;
class QStyleOptionGraphicsItem;
class QWidget;

namespace novel::presentation {

class PersonNodeItem final : public QGraphicsObject {
    Q_OBJECT

public:
    enum class VisualState {
        Normal,
        Dimmed,
        Adjacent,
        Selected,
        Traversal
    };

    enum { Type = QGraphicsItem::UserType + 101 };

    explicit PersonNodeItem(PersonId id,
                            QString canonicalName,
                            std::uint32_t chapterCount,
                            qreal radius,
                            QGraphicsItem* parent = nullptr);

    [[nodiscard]] int type() const override;
    [[nodiscard]] QRectF boundingRect() const override;
    [[nodiscard]] QPainterPath shape() const override;
    void paint(QPainter* painter,
               const QStyleOptionGraphicsItem* option,
               QWidget* widget = nullptr) override;

    [[nodiscard]] PersonId personId() const noexcept;
    [[nodiscard]] const QString& canonicalName() const noexcept;
    [[nodiscard]] std::uint32_t chapterCount() const noexcept;
    [[nodiscard]] qreal radius() const noexcept;
    [[nodiscard]] VisualState visualState() const noexcept;

    void setVisualState(VisualState state);

signals:
    void activated(novel::PersonId personId);
    void positionChanged(novel::PersonId personId, const QPointF& scenePosition);

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    PersonId personId_{};
    QString canonicalName_;
    std::uint32_t chapterCount_{};
    qreal radius_{};
    VisualState visualState_{VisualState::Normal};
    bool hovered_{false};
    bool selectedWhenPressed_{false};
    QPointF pressScenePosition_;
};

}  // namespace novel::presentation
