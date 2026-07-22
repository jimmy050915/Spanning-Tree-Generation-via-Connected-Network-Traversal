#pragma once

#include "application/ApplicationDtos.h"

#include <QHash>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QVector>

class QGraphicsScene;
class QGraphicsItem;

namespace novel::presentation {

class PersonNodeItem;
class RelationEdgeItem;

class RelationGraphSceneController final : public QObject {
    Q_OBJECT

public:
    explicit RelationGraphSceneController(QGraphicsScene* scene,
                                          QObject* parent = nullptr);

    void setSnapshot(const application::GraphSnapshotDto& snapshot);
    void clear();

    [[nodiscard]] bool focusPerson(PersonId personId);
    [[nodiscard]] bool focusPerson(const QString& canonicalName);
    void setTraversalHighlight(PersonId personId,
                               PersonId previousPersonId = PersonId{});
    void clearTraversalHighlight();

    [[nodiscard]] QGraphicsScene* scene() const noexcept;
    [[nodiscard]] PersonNodeItem* nodeItem(PersonId personId) const noexcept;
    [[nodiscard]] RelationEdgeItem* edgeItem(EdgeId edgeId) const noexcept;
    [[nodiscard]] QList<PersonId> personIds() const;
    [[nodiscard]] qsizetype nodeCount() const noexcept;
    [[nodiscard]] qsizetype edgeCount() const noexcept;
    [[nodiscard]] std::uint64_t revision() const noexcept;
    [[nodiscard]] double minimumJaccard() const noexcept;

    [[nodiscard]] static QHash<PersonId, QPointF> circularLayout(
        const std::vector<application::GraphNodeDto>& nodes);

signals:
    void personSelected(novel::PersonId personId);
    void relationSelected(novel::EdgeId edgeId,
                          novel::PersonId endpointA,
                          novel::PersonId endpointB);
    void selectionCleared();
    void snapshotRendered(std::uint64_t revision,
                          qsizetype nodeCount,
                          qsizetype edgeCount);

private slots:
    void handleSelectionChanged();

private:
    void applySelectionHighlight();
    void selectOnly(QGraphicsItem* item);
    [[nodiscard]] static qreal nodeRadius(std::uint32_t chapterCount,
                                          std::uint32_t minimum,
                                          std::uint32_t maximum) noexcept;

    QPointer<QGraphicsScene> scene_;
    QHash<PersonId, PersonNodeItem*> nodes_;
    QHash<EdgeId, RelationEdgeItem*> edgesById_;
    QVector<RelationEdgeItem*> edges_;
    std::uint64_t revision_{};
    double minimumJaccard_{};
    PersonId traversalPerson_{};
    PersonId previousTraversalPerson_{};
    bool rebuilding_{false};
};

}  // namespace novel::presentation
