#pragma once

#include "application/ApplicationDtos.h"

#include <QGraphicsView>

class QGraphicsScene;
class QPainter;
class QResizeEvent;
class QWheelEvent;

namespace novel::presentation {

class TraversalTreeSceneController;

class TraversalTreeView final : public QGraphicsView {
    Q_OBJECT

public:
    explicit TraversalTreeView(QWidget* parent = nullptr);
    ~TraversalTreeView() override;

    void setTraversal(const application::TraversalResultDto& traversal);
    void clear();
    [[nodiscard]] bool focusPerson(PersonId personId);
    void resetView();

    [[nodiscard]] TraversalTreeSceneController* sceneController() const noexcept;

signals:
    void personSelected(novel::PersonId personId);
    void selectionCleared();

protected:
    void wheelEvent(QWheelEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void drawBackground(QPainter* painter, const QRectF& rect) override;

private:
    QGraphicsScene* ownedScene_{};
    TraversalTreeSceneController* controller_{};
    bool fittingView_{false};
    qreal minimumScale_{0.2};
    qreal maximumScale_{5.0};
};

}  // namespace novel::presentation
