#pragma once

#include "application/ApplicationDtos.h"

#include <QGraphicsView>
#include <QPoint>
#include <QTimer>

#include <vector>

class QGraphicsScene;
class QMouseEvent;
class QPainter;
class QResizeEvent;
class QWheelEvent;

namespace novel::presentation {

class RelationGraphSceneController;

class RelationGraphView final : public QGraphicsView {
    Q_OBJECT

public:
    explicit RelationGraphView(QWidget* parent = nullptr);
    ~RelationGraphView() override;

    void setSnapshot(const application::GraphSnapshotDto& snapshot);
    void clear();

    [[nodiscard]] bool focusPerson(PersonId personId);
    [[nodiscard]] bool focusPerson(const QString& canonicalName);
    void setTraversalHighlight(PersonId personId,
                               PersonId previousPersonId = PersonId{});
    void clearTraversalHighlight();
    void playTraversal(const application::TraversalResultDto& traversal,
                       int intervalMilliseconds = 400);
    void playTraversalOrder(const std::vector<PersonId>& order,
                            int intervalMilliseconds = 400);
    void stopTraversal();
    void resetView();

    [[nodiscard]] bool isTraversalPlaying() const noexcept;
    [[nodiscard]] RelationGraphSceneController* sceneController() const noexcept;

signals:
    void personSelected(novel::PersonId personId);
    void relationSelected(novel::EdgeId edgeId,
                          novel::PersonId endpointA,
                          novel::PersonId endpointB);
    void selectionCleared();
    void traversalStep(novel::PersonId personId, int index);
    void traversalFinished();
    void traversalStopped();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;

private slots:
    void advanceTraversal();

private:
    void cancelTraversal(bool notify);

    QGraphicsScene* ownedScene_{};
    RelationGraphSceneController* controller_{};
    QTimer animationTimer_;
    std::vector<PersonId> traversalOrder_;
    std::size_t nextTraversalIndex_{};
    bool middleButtonPanning_{false};
    bool fittingView_{false};
    QPoint lastPanPosition_;
    qreal minimumScale_{0.15};
    qreal maximumScale_{6.0};
};

}  // namespace novel::presentation
