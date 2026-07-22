#include "presentation/graphics/TraversalTreeSceneController.h"

#include <QApplication>
#include <QFontMetricsF>
#include <QGraphicsItem>
#include <QGraphicsObject>
#include <QGraphicsScene>
#include <QPainter>
#include <QPalette>
#include <QPen>
#include <QQueue>
#include <QSet>
#include <QSignalBlocker>

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace novel::presentation {

namespace {

QString fromUtf8(const std::string& value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

class TraversalTreeNodeItem final : public QGraphicsObject {
public:
    enum { Type = QGraphicsItem::UserType + 103 };

    TraversalTreeNodeItem(PersonId personId,
                          QString label,
                          bool virtualRoot,
                          QGraphicsItem* parent = nullptr)
        : QGraphicsObject(parent),
          personId_(personId),
          label_(std::move(label)),
          virtualRoot_(virtualRoot) {
        const QFontMetricsF metrics(QApplication::font());
        width_ = std::clamp<qreal>(metrics.horizontalAdvance(label_) + 30.0,
                                   96.0, 180.0);
        setFlag(ItemIsSelectable, !virtualRoot_);
        setToolTip(virtualRoot_ ? QObject::tr("全部人物虚拟根") : label_);
    }

    [[nodiscard]] int type() const override { return Type; }
    [[nodiscard]] QRectF boundingRect() const override {
        return {-width_ / 2.0 - 2.0, -26.0, width_ + 4.0, 52.0};
    }
    [[nodiscard]] PersonId personId() const noexcept { return personId_; }

    void paint(QPainter* painter,
               const QStyleOptionGraphicsItem*,
               QWidget*) override {
        const auto palette = QApplication::palette();
        QColor fill = virtualRoot_ ? palette.alternateBase().color()
                                   : palette.button().color();
        QColor outline = palette.mid().color();
        QColor text = palette.buttonText().color();
        if (isSelected()) {
            fill = palette.highlight().color();
            outline = fill.darker(125);
            text = palette.highlightedText().color();
        }

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        QPen pen(outline, virtualRoot_ ? 1.5 : 1.2);
        if (virtualRoot_) {
            pen.setStyle(Qt::DashLine);
        }
        painter->setPen(pen);
        painter->setBrush(fill);
        painter->drawRoundedRect(QRectF{-width_ / 2.0, -24.0, width_, 48.0},
                                 8.0, 8.0);
        painter->setPen(text);
        QFont font = painter->font();
        font.setBold(virtualRoot_ || isSelected());
        painter->setFont(font);
        const QFontMetricsF metrics(font);
        const auto renderedLabel = metrics.elidedText(
            label_, Qt::ElideRight, width_ - 18.0);
        painter->drawText(QRectF{-width_ / 2.0 + 8.0, -24.0,
                                 width_ - 16.0, 48.0},
                          Qt::AlignCenter, renderedLabel);
        painter->restore();
    }

private:
    PersonId personId_{};
    QString label_;
    bool virtualRoot_{};
    qreal width_{96.0};
};

struct RenderedTreeEdge {
    PersonId parent{};
    PersonId child{};
};

}  // namespace

TraversalTreeSceneController::TraversalTreeSceneController(QGraphicsScene* scene,
                                                           QObject* parent)
    : QObject(parent), scene_(scene) {
    Q_ASSERT(scene != nullptr);
    connect(scene, &QGraphicsScene::selectionChanged,
            this, &TraversalTreeSceneController::handleSelectionChanged);
}

void TraversalTreeSceneController::setTraversal(
    const application::TraversalResultDto& traversal) {
    if (scene_ == nullptr) {
        return;
    }

    rebuilding_ = true;
    const QSignalBlocker sceneBlocker(scene_);
    nodeItems_.clear();
    nodePositions_.clear();
    nodeLabels_.clear();
    edgeCount_ = 0;
    virtualRootId_ = {};
    hasVirtualRoot_ = false;
    scene_->clear();

    QHash<PersonId, application::TraversalNodeDto> nodesById;
    for (const auto& node : traversal.nodes) {
        if (nodesById.contains(node.id)) {
            continue;
        }
        nodesById.insert(node.id, node);
        if (node.virtualRoot && !hasVirtualRoot_) {
            hasVirtualRoot_ = true;
            virtualRootId_ = node.id;
        }
    }

    QHash<PersonId, int> visitRank;
    for (std::size_t index = 0; index < traversal.order.size(); ++index) {
        if (!visitRank.contains(traversal.order[index])) {
            visitRank.insert(traversal.order[index], static_cast<int>(index));
        }
    }
    const auto rankOf = [&](PersonId id) {
        return visitRank.value(id, std::numeric_limits<int>::max());
    };
    const auto lessByVisit = [&](PersonId left, PersonId right) {
        const auto leftRank = rankOf(left);
        const auto rightRank = rankOf(right);
        return leftRank == rightRank ? left < right : leftRank < rightRank;
    };

    QHash<PersonId, QList<PersonId>> children;
    QHash<PersonId, int> indegree;
    for (auto iterator = nodesById.cbegin(); iterator != nodesById.cend(); ++iterator) {
        indegree.insert(iterator.key(), 0);
    }

    std::vector<RenderedTreeEdge> renderedEdges;
    QSet<quint64> seenEdges;
    const auto edgeKey = [](PersonId parent, PersonId child) {
        return (static_cast<quint64>(parent) << 32U) |
               static_cast<quint64>(child);
    };
    for (const auto& edge : traversal.treeEdges) {
        if (!nodesById.contains(edge.parent) || !nodesById.contains(edge.child) ||
            edge.parent == edge.child || seenEdges.contains(edgeKey(edge.parent, edge.child))) {
            continue;
        }
        seenEdges.insert(edgeKey(edge.parent, edge.child));
        children[edge.parent].push_back(edge.child);
        indegree[edge.child] = indegree.value(edge.child) + 1;
        renderedEdges.push_back({edge.parent, edge.child});
    }

    if (hasVirtualRoot_) {
        QList<PersonId> componentRoots;
        for (auto iterator = nodesById.cbegin(); iterator != nodesById.cend(); ++iterator) {
            if (iterator.key() != virtualRootId_ && indegree.value(iterator.key()) == 0) {
                componentRoots.push_back(iterator.key());
            }
        }
        std::sort(componentRoots.begin(), componentRoots.end(), lessByVisit);
        for (const auto root : componentRoots) {
            if (!seenEdges.contains(edgeKey(virtualRootId_, root))) {
                seenEdges.insert(edgeKey(virtualRootId_, root));
                children[virtualRootId_].push_back(root);
                indegree[root] = indegree.value(root) + 1;
                renderedEdges.push_back({virtualRootId_, root});
            }
        }
    }

    for (auto iterator = children.begin(); iterator != children.end(); ++iterator) {
        std::sort(iterator.value().begin(), iterator.value().end(), lessByVisit);
    }

    QHash<PersonId, int> depth;
    QQueue<PersonId> queue;
    if (hasVirtualRoot_) {
        depth.insert(virtualRootId_, 0);
        queue.enqueue(virtualRootId_);
    } else {
        QList<PersonId> roots;
        for (auto iterator = nodesById.cbegin(); iterator != nodesById.cend(); ++iterator) {
            if (indegree.value(iterator.key()) == 0) {
                roots.push_back(iterator.key());
            }
        }
        std::sort(roots.begin(), roots.end(), lessByVisit);
        for (const auto root : roots) {
            depth.insert(root, 0);
            queue.enqueue(root);
        }
    }

    while (!queue.isEmpty()) {
        const auto parent = queue.dequeue();
        for (const auto child : children.value(parent)) {
            if (depth.contains(child)) {
                continue;
            }
            depth.insert(child, depth.value(parent) + 1);
            queue.enqueue(child);
        }
    }
    for (auto iterator = nodesById.cbegin(); iterator != nodesById.cend(); ++iterator) {
        if (!depth.contains(iterator.key())) {
            depth.insert(iterator.key(), hasVirtualRoot_ ? 1 : 0);
        }
    }

    QHash<int, QList<PersonId>> layers;
    int maximumDepth = 0;
    for (auto iterator = depth.cbegin(); iterator != depth.cend(); ++iterator) {
        layers[iterator.value()].push_back(iterator.key());
        maximumDepth = std::max(maximumDepth, iterator.value());
    }
    for (int layer = 0; layer <= maximumDepth; ++layer) {
        auto& ids = layers[layer];
        std::sort(ids.begin(), ids.end(), lessByVisit);
        for (qsizetype index = 0; index < ids.size(); ++index) {
            const auto x = (static_cast<qreal>(index) -
                            static_cast<qreal>(ids.size() - 1) / 2.0) * 150.0;
            nodePositions_.insert(ids[index], QPointF{x, layer * 105.0});
        }
    }

    std::vector<PersonId> orderedNodeIds;
    orderedNodeIds.reserve(static_cast<std::size_t>(nodesById.size()));
    for (auto iterator = nodesById.cbegin(); iterator != nodesById.cend(); ++iterator) {
        orderedNodeIds.push_back(iterator.key());
    }
    std::sort(orderedNodeIds.begin(), orderedNodeIds.end(), [&](PersonId left, PersonId right) {
        const auto leftDepth = depth.value(left);
        const auto rightDepth = depth.value(right);
        if (leftDepth != rightDepth) {
            return leftDepth < rightDepth;
        }
        return lessByVisit(left, right);
    });

    for (const auto id : orderedNodeIds) {
        const auto node = nodesById.value(id);
        auto label = fromUtf8(node.canonicalName);
        if (node.virtualRoot && label.trimmed().isEmpty()) {
            label = tr("全部人物");
        }
        auto* item = new TraversalTreeNodeItem(id, label, node.virtualRoot);
        scene_->addItem(item);
        item->setPos(nodePositions_.value(id));
        item->setZValue(1.0);
        nodeItems_.insert(id, item);
        nodeLabels_.insert(id, label);
    }

    for (const auto& edge : renderedEdges) {
        auto* parent = nodeItems_.value(edge.parent, nullptr);
        auto* child = nodeItems_.value(edge.child, nullptr);
        if (parent == nullptr || child == nullptr) {
            continue;
        }
        auto edgeColor = QApplication::palette().text().color();
        edgeColor.setAlphaF(0.38);
        auto* line = scene_->addLine(QLineF(parent->pos(), child->pos()),
                                     QPen(edgeColor, 1.6));
        line->setZValue(0.0);
        ++edgeCount_;
    }

    if (scene_->items().isEmpty()) {
        scene_->setSceneRect(-100.0, -80.0, 200.0, 160.0);
    } else {
        scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-55.0, -50.0,
                                                                  55.0, 50.0));
    }
    rebuilding_ = false;
    emit traversalRendered(nodeItems_.size(), edgeCount_);
}

void TraversalTreeSceneController::clear() {
    setTraversal(application::TraversalResultDto{});
}

bool TraversalTreeSceneController::focusPerson(PersonId personId) {
    auto* item = nodeItems_.value(personId, nullptr);
    if (item == nullptr || (hasVirtualRoot_ && personId == virtualRootId_)) {
        return false;
    }
    if (scene_ != nullptr) {
        scene_->clearSelection();
    }
    item->setSelected(true);
    return true;
}

QGraphicsScene* TraversalTreeSceneController::scene() const noexcept {
    return scene_;
}

qsizetype TraversalTreeSceneController::nodeCount() const noexcept {
    return nodeItems_.size();
}

qsizetype TraversalTreeSceneController::edgeCount() const noexcept {
    return edgeCount_;
}

bool TraversalTreeSceneController::hasVirtualRoot() const noexcept {
    return hasVirtualRoot_;
}

QPointF TraversalTreeSceneController::nodePosition(PersonId personId) const {
    return nodePositions_.value(personId);
}

QString TraversalTreeSceneController::nodeLabel(PersonId personId) const {
    return nodeLabels_.value(personId);
}

QList<PersonId> TraversalTreeSceneController::nodeIds() const {
    auto result = nodeItems_.keys();
    std::sort(result.begin(), result.end());
    return result;
}

void TraversalTreeSceneController::handleSelectionChanged() {
    if (rebuilding_ || scene_ == nullptr) {
        return;
    }
    const auto selected = scene_->selectedItems();
    if (selected.isEmpty()) {
        emit selectionCleared();
        return;
    }
    auto* item = selected.front();
    if (item->type() != TraversalTreeNodeItem::Type) {
        return;
    }
    const auto personId = static_cast<TraversalTreeNodeItem*>(item)->personId();
    if (!hasVirtualRoot_ || personId != virtualRootId_) {
        emit personSelected(personId);
    }
}

}  // namespace novel::presentation
