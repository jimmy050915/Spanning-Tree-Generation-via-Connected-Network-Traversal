#include "application/ApplicationDtos.h"
#include "presentation/graphics/PersonNodeItem.h"
#include "presentation/graphics/RelationEdgeItem.h"
#include "presentation/graphics/RelationGraphSceneController.h"
#include "presentation/graphics/RelationGraphView.h"
#include "presentation/graphics/TraversalTreeSceneController.h"
#include "presentation/graphics/TraversalTreeView.h"

#include <QGraphicsScene>
#include <QSignalSpy>
#include <QtTest>

#include <cmath>

namespace novel::presentation {

namespace {

application::GraphSnapshotDto graphFixture() {
    application::GraphSnapshotDto snapshot;
    snapshot.revision = 17;
    snapshot.minimumJaccard = 0.2;
    snapshot.nodes = {
        {3, "\xE7\x8E\x8B\xE4\xBA\x94", 4},
        {1, "\xE5\xBC\xA0\xE4\xB8\x89", 1},
        {2, "\xE6\x9D\x8E\xE5\x9B\x9B", 9},
    };
    snapshot.edges = {
        {12, 2, 3, "\xE6\x9D\x8E\xE5\x9B\x9B", "\xE7\x8E\x8B\xE4\xBA\x94", 4, 0.8},
        {11, 1, 2, "\xE5\xBC\xA0\xE4\xB8\x89", "\xE6\x9D\x8E\xE5\x9B\x9B", 2, 0.2},
    };
    return snapshot;
}

application::TraversalResultDto traversalFixture() {
    application::TraversalResultDto traversal;
    traversal.kind = application::TraversalKind::DepthFirst;
    traversal.start = 1;
    traversal.order = {1, 2, 3};
    traversal.orderNames = {
        "\xE5\xBC\xA0\xE4\xB8\x89",
        "\xE6\x9D\x8E\xE5\x9B\x9B",
        "\xE7\x8E\x8B\xE4\xBA\x94",
    };
    traversal.nodes = {
        {0, "", true},
        {1, "\xE5\xBC\xA0\xE4\xB8\x89", false},
        {2, "\xE6\x9D\x8E\xE5\x9B\x9B", false},
        {3, "\xE7\x8E\x8B\xE4\xBA\x94", false},
    };
    traversal.treeEdges = {{0, 1}, {1, 2}, {0, 3}};
    return traversal;
}

}  // namespace

class GraphicsPhaseFiveTest final : public QObject {
    Q_OBJECT

private slots:
    void circularLayoutIsStableAndIdOrdered();
    void graphControllerRendersScaleAndMoveUpdates();
    void selectionHighlightsAdjacencyAndEmitsDetails();
    void viewSearchAndTraversalAnimationWork();
    void traversalTreeUsesVirtualRootAndLayers();
};

void GraphicsPhaseFiveTest::circularLayoutIsStableAndIdOrdered() {
    const auto fixture = graphFixture();
    const auto first = RelationGraphSceneController::circularLayout(fixture.nodes);
    const auto second = RelationGraphSceneController::circularLayout(fixture.nodes);

    QCOMPARE(first.size(), 3);
    QCOMPARE(first, second);
    QVERIFY(std::abs(first.value(1).x()) < 0.0001);
    QVERIFY(first.value(1).y() < 0.0);
    QVERIFY(first.value(2).x() > 0.0);
    QVERIFY(first.value(3).x() < 0.0);
}

void GraphicsPhaseFiveTest::graphControllerRendersScaleAndMoveUpdates() {
    QGraphicsScene scene;
    RelationGraphSceneController controller(&scene);
    controller.setSnapshot(graphFixture());

    QCOMPARE(controller.nodeCount(), 3);
    QCOMPARE(controller.edgeCount(), 2);
    QCOMPARE(controller.revision(), std::uint64_t{17});
    QCOMPARE(controller.minimumJaccard(), 0.2);
    QVERIFY(controller.nodeItem(2)->radius() > controller.nodeItem(1)->radius());
    QVERIFY(controller.edgeItem(12)->normalWidth() >
            controller.edgeItem(11)->normalWidth());

    const auto before = controller.edgeItem(11)->boundingRect();
    controller.nodeItem(1)->setPos(controller.nodeItem(1)->pos() + QPointF{80.0, 35.0});
    QCoreApplication::processEvents();
    QVERIFY(before != controller.edgeItem(11)->boundingRect());
}

void GraphicsPhaseFiveTest::selectionHighlightsAdjacencyAndEmitsDetails() {
    QGraphicsScene scene;
    RelationGraphSceneController controller(&scene);
    controller.setSnapshot(graphFixture());
    QSignalSpy personSpy(&controller, &RelationGraphSceneController::personSelected);
    QSignalSpy relationSpy(&controller, &RelationGraphSceneController::relationSelected);

    QVERIFY(controller.focusPerson(1));
    QCOMPARE(personSpy.count(), 1);
    QCOMPARE(controller.nodeItem(1)->visualState(),
             PersonNodeItem::VisualState::Selected);
    QCOMPARE(controller.nodeItem(2)->visualState(),
             PersonNodeItem::VisualState::Adjacent);
    QCOMPARE(controller.nodeItem(3)->visualState(),
             PersonNodeItem::VisualState::Dimmed);
    QCOMPARE(controller.edgeItem(11)->visualState(),
             RelationEdgeItem::VisualState::Adjacent);

    scene.clearSelection();
    controller.edgeItem(12)->setSelected(true);
    QCOMPARE(relationSpy.count(), 1);
    QCOMPARE(controller.edgeItem(12)->visualState(),
             RelationEdgeItem::VisualState::Selected);
    QCOMPARE(controller.nodeItem(2)->visualState(),
             PersonNodeItem::VisualState::Selected);
    QCOMPARE(controller.nodeItem(3)->visualState(),
             PersonNodeItem::VisualState::Selected);
}

void GraphicsPhaseFiveTest::viewSearchAndTraversalAnimationWork() {
    RelationGraphView view;
    view.resize(280, 220);
    view.setSnapshot(graphFixture());
    view.resize(800, 600);
    view.show();
    QCoreApplication::processEvents();
    const auto renderedBounds =
        view.mapFromScene(view.scene()->itemsBoundingRect()).boundingRect();
    QVERIFY(renderedBounds.width() > view.viewport()->width() / 2);

    QTest::mouseMove(view.viewport(), QPoint{4, 4});
    QCoreApplication::processEvents();
    const auto beforeHover = view.viewport()->grab().toImage();
    const auto* hoverEdge = view.sceneController()->edgeItem(12);
    QVERIFY(hoverEdge);
    QTest::mouseMove(
        view.viewport(),
        view.mapFromScene(hoverEdge->boundingRect().center()));
    QTest::qWait(20);
    const auto afterHover = view.viewport()->grab().toImage();
    QVERIFY(beforeHover != afterHover);

    QVERIFY(view.focusPerson(QString::fromUtf8("\xE6\x9D\x8E")));
    QCOMPARE(view.sceneController()->nodeItem(2)->visualState(),
             PersonNodeItem::VisualState::Selected);

    QSignalSpy stepSpy(&view, &RelationGraphView::traversalStep);
    QSignalSpy finishedSpy(&view, &RelationGraphView::traversalFinished);
    view.playTraversal(traversalFixture(), 5);
    QTRY_COMPARE_WITH_TIMEOUT(finishedSpy.count(), 1, 500);
    QCOMPARE(stepSpy.count(), 3);
    QCOMPARE(view.sceneController()->nodeItem(3)->visualState(),
             PersonNodeItem::VisualState::Normal);
    QVERIFY(!view.isTraversalPlaying());

    QSignalSpy stoppedSpy(&view, &RelationGraphView::traversalStopped);
    view.playTraversal(traversalFixture(), 100);
    QVERIFY(view.isTraversalPlaying());
    QCOMPARE(view.sceneController()->nodeItem(1)->visualState(),
             PersonNodeItem::VisualState::Traversal);
    view.stopTraversal();
    QCOMPARE(stoppedSpy.count(), 1);
    QVERIFY(!view.isTraversalPlaying());
    QCOMPARE(view.sceneController()->nodeItem(1)->visualState(),
             PersonNodeItem::VisualState::Normal);
}

void GraphicsPhaseFiveTest::traversalTreeUsesVirtualRootAndLayers() {
    TraversalTreeView view;
    view.resize(280, 180);
    view.setTraversal(traversalFixture());
    view.resize(800, 600);
    view.show();
    QCoreApplication::processEvents();
    const auto renderedBounds =
        view.mapFromScene(view.scene()->itemsBoundingRect()).boundingRect();
    QVERIFY(renderedBounds.height() > view.viewport()->height() / 2);
    auto* controller = view.sceneController();

    QCOMPARE(controller->nodeCount(), 4);
    QCOMPARE(controller->edgeCount(), 3);
    QVERIFY(controller->hasVirtualRoot());
    QCOMPARE(controller->nodeLabel(0), QString::fromUtf8("\xE5\x85\xA8\xE9\x83\xA8\xE4\xBA\xBA\xE7\x89\xA9"));
    QVERIFY(controller->nodePosition(0).y() < controller->nodePosition(1).y());
    QVERIFY(controller->nodePosition(1).y() < controller->nodePosition(2).y());
    QCOMPARE(controller->nodePosition(1).y(), controller->nodePosition(3).y());

    QSignalSpy selectedSpy(controller, &TraversalTreeSceneController::personSelected);
    QVERIFY(view.focusPerson(2));
    QCOMPARE(selectedSpy.count(), 1);
    QVERIFY(!view.focusPerson(0));
}

}  // namespace novel::presentation

QTEST_MAIN(novel::presentation::GraphicsPhaseFiveTest)
#include "test_graphics_phase_five.moc"
