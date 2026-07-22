#pragma once

#include "application/ApplicationDtos.h"

#include <QHash>
#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QString>

class QGraphicsItem;
class QGraphicsScene;

namespace novel::presentation {

class TraversalTreeSceneController final : public QObject {
    Q_OBJECT

public:
    explicit TraversalTreeSceneController(QGraphicsScene* scene,
                                          QObject* parent = nullptr);

    void setTraversal(const application::TraversalResultDto& traversal);
    void clear();
    [[nodiscard]] bool focusPerson(PersonId personId);

    [[nodiscard]] QGraphicsScene* scene() const noexcept;
    [[nodiscard]] qsizetype nodeCount() const noexcept;
    [[nodiscard]] qsizetype edgeCount() const noexcept;
    [[nodiscard]] bool hasVirtualRoot() const noexcept;
    [[nodiscard]] QPointF nodePosition(PersonId personId) const;
    [[nodiscard]] QString nodeLabel(PersonId personId) const;
    [[nodiscard]] QList<PersonId> nodeIds() const;

signals:
    void personSelected(novel::PersonId personId);
    void selectionCleared();
    void traversalRendered(qsizetype nodeCount, qsizetype edgeCount);

private slots:
    void handleSelectionChanged();

private:
    QPointer<QGraphicsScene> scene_;
    QHash<PersonId, QGraphicsItem*> nodeItems_;
    QHash<PersonId, QPointF> nodePositions_;
    QHash<PersonId, QString> nodeLabels_;
    qsizetype edgeCount_{};
    PersonId virtualRootId_{};
    bool hasVirtualRoot_{false};
    bool rebuilding_{false};
};

}  // namespace novel::presentation
