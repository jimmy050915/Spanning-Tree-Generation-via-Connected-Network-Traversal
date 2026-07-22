#pragma once

#include "domain/model/GraphTypes.h"

#include <QGraphicsObject>
#include <QLineF>
#include <QString>

class QGraphicsSceneHoverEvent;
class QGraphicsSceneMouseEvent;
class QPainter;
class QPainterPath;
class QStyleOptionGraphicsItem;
class QWidget;

namespace novel::presentation {

class PersonNodeItem;

class RelationEdgeItem final : public QGraphicsObject {
    Q_OBJECT

public:
    enum class VisualState {
        Normal,
        Dimmed,
        Adjacent,
        Selected,
        Traversal
    };

    enum { Type = QGraphicsItem::UserType + 102 };

    RelationEdgeItem(EdgeId edgeId,
                     PersonNodeItem* endpointA,
                     PersonNodeItem* endpointB,
                     QString endpointAName,
                     QString endpointBName,
                     std::uint32_t coChapterCount,
                     double jaccard,
                     QGraphicsItem* parent = nullptr);

    [[nodiscard]] int type() const override;
    [[nodiscard]] QRectF boundingRect() const override;
    [[nodiscard]] QPainterPath shape() const override;
    void paint(QPainter* painter,
               const QStyleOptionGraphicsItem* option,
               QWidget* widget = nullptr) override;

    [[nodiscard]] EdgeId edgeId() const noexcept;
    [[nodiscard]] PersonId endpointA() const noexcept;
    [[nodiscard]] PersonId endpointB() const noexcept;
    [[nodiscard]] std::uint32_t coChapterCount() const noexcept;
    [[nodiscard]] double jaccard() const noexcept;
    [[nodiscard]] qreal normalWidth() const noexcept;
    [[nodiscard]] VisualState visualState() const noexcept;
    [[nodiscard]] bool connects(PersonId person) const noexcept;
    [[nodiscard]] bool connects(PersonId first, PersonId second) const noexcept;

    void setVisualState(VisualState state);

public slots:
    void adjust();

signals:
    void activated(novel::EdgeId edgeId,
                   novel::PersonId endpointA,
                   novel::PersonId endpointB);

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    [[nodiscard]] qreal effectiveWidth() const noexcept;

    EdgeId edgeId_{};
    PersonNodeItem* endpointAItem_{};  // Owned by the scene.
    PersonNodeItem* endpointBItem_{};  // Owned by the scene.
    QString endpointAName_;
    QString endpointBName_;
    std::uint32_t coChapterCount_{};
    double jaccard_{};
    QLineF line_;
    VisualState visualState_{VisualState::Normal};
    bool hovered_{false};
    bool selectedWhenPressed_{false};
};

}  // namespace novel::presentation
