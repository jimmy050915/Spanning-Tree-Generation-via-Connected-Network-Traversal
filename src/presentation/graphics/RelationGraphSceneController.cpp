#include "presentation/graphics/RelationGraphSceneController.h"

#include "presentation/graphics/PersonNodeItem.h"
#include "presentation/graphics/RelationEdgeItem.h"

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QSignalBlocker>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace novel::presentation {

namespace {

QString fromUtf8(const std::string& value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

constexpr qreal kPi = 3.14159265358979323846;

}  // namespace

RelationGraphSceneController::RelationGraphSceneController(QGraphicsScene* scene,
                                                           QObject* parent)
    : QObject(parent), scene_(scene) {
    Q_ASSERT(scene != nullptr);
    connect(scene, &QGraphicsScene::selectionChanged,
            this, &RelationGraphSceneController::handleSelectionChanged);
}

void RelationGraphSceneController::setSnapshot(
    const application::GraphSnapshotDto& snapshot) {
    if (scene_ == nullptr) {
        return;
    }

    rebuilding_ = true;
    const QSignalBlocker sceneBlocker(scene_);
    nodes_.clear();
    edgesById_.clear();
    edges_.clear();
    scene_->clear();
    traversalPerson_ = {};
    previousTraversalPerson_ = {};
    revision_ = snapshot.revision;
    minimumJaccard_ = snapshot.minimumJaccard;

    std::uint32_t minimumCount = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t maximumCount = 0;
    for (const auto& node : snapshot.nodes) {
        minimumCount = std::min(minimumCount, node.chapterCount);
        maximumCount = std::max(maximumCount, node.chapterCount);
    }
    if (snapshot.nodes.empty()) {
        minimumCount = 0;
    }

    const auto positions = circularLayout(snapshot.nodes);
    std::vector<const application::GraphNodeDto*> sortedNodes;
    sortedNodes.reserve(snapshot.nodes.size());
    for (const auto& node : snapshot.nodes) {
        sortedNodes.push_back(&node);
    }
    std::stable_sort(sortedNodes.begin(), sortedNodes.end(),
                     [](const auto* left, const auto* right) {
                         if (left->id != right->id) {
                             return left->id < right->id;
                         }
                         return left->canonicalName < right->canonicalName;
                     });

    for (const auto* node : sortedNodes) {
        auto* item = new PersonNodeItem(
            node->id, fromUtf8(node->canonicalName), node->chapterCount,
            nodeRadius(node->chapterCount, minimumCount, maximumCount));
        scene_->addItem(item);
        item->setPos(positions.value(node->id));
        nodes_.insert(node->id, item);
        connect(item, &PersonNodeItem::positionChanged, this,
                [this, item](PersonId, const QPointF&) {
                    if (scene_ == nullptr) {
                        return;
                    }
                    const auto expandedItemBounds =
                        item->sceneBoundingRect().adjusted(-90.0, -90.0,
                                                           90.0, 90.0);
                    scene_->setSceneRect(scene_->sceneRect().united(expandedItemBounds));
                });
        connect(item, &PersonNodeItem::activated,
                this, &RelationGraphSceneController::personSelected);
    }

    std::vector<const application::GraphEdgeDto*> sortedEdges;
    sortedEdges.reserve(snapshot.edges.size());
    for (const auto& edge : snapshot.edges) {
        sortedEdges.push_back(&edge);
    }
    std::stable_sort(sortedEdges.begin(), sortedEdges.end(),
                     [](const auto* left, const auto* right) {
                         if (left->id != right->id) {
                             return left->id < right->id;
                         }
                         if (left->endpointA != right->endpointA) {
                             return left->endpointA < right->endpointA;
                         }
                         return left->endpointB < right->endpointB;
                     });

    for (const auto* edge : sortedEdges) {
        auto* endpointA = nodes_.value(edge->endpointA, nullptr);
        auto* endpointB = nodes_.value(edge->endpointB, nullptr);
        if (endpointA == nullptr || endpointB == nullptr || endpointA == endpointB) {
            continue;
        }

        auto* item = new RelationEdgeItem(
            edge->id, endpointA, endpointB, fromUtf8(edge->endpointAName),
            fromUtf8(edge->endpointBName), edge->coChapterCount, edge->jaccard);
        scene_->addItem(item);
        item->adjust();
        connect(endpointA, &PersonNodeItem::positionChanged,
                item, &RelationEdgeItem::adjust);
        connect(endpointB, &PersonNodeItem::positionChanged,
                item, &RelationEdgeItem::adjust);
        edges_.push_back(item);
        edgesById_.insert(edge->id, item);
        connect(item, &RelationEdgeItem::activated,
                this, &RelationGraphSceneController::relationSelected);
    }

    if (scene_->items().isEmpty()) {
        scene_->setSceneRect(-100.0, -100.0, 200.0, 200.0);
    } else {
        scene_->setSceneRect(scene_->itemsBoundingRect().adjusted(-90.0, -90.0,
                                                                  90.0, 90.0));
    }
    rebuilding_ = false;
    applySelectionHighlight();
    emit snapshotRendered(revision_, nodes_.size(), edges_.size());
}

void RelationGraphSceneController::clear() {
    application::GraphSnapshotDto empty;
    setSnapshot(empty);
}

bool RelationGraphSceneController::focusPerson(PersonId personId) {
    auto* item = nodeItem(personId);
    if (item == nullptr) {
        return false;
    }
    clearTraversalHighlight();
    selectOnly(item);
    return true;
}

bool RelationGraphSceneController::focusPerson(const QString& canonicalName) {
    const auto needle = canonicalName.trimmed();
    if (needle.isEmpty()) {
        return false;
    }

    QList<PersonNodeItem*> ordered = nodes_.values();
    std::sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right) {
        return left->personId() < right->personId();
    });

    auto findMatch = [&](auto predicate) -> PersonNodeItem* {
        for (auto* node : ordered) {
            if (predicate(node->canonicalName())) {
                return node;
            }
        }
        return nullptr;
    };

    auto* match = findMatch([&](const QString& name) {
        return name.compare(needle, Qt::CaseInsensitive) == 0;
    });
    if (match == nullptr) {
        match = findMatch([&](const QString& name) {
            return name.startsWith(needle, Qt::CaseInsensitive);
        });
    }
    if (match == nullptr) {
        match = findMatch([&](const QString& name) {
            return name.contains(needle, Qt::CaseInsensitive);
        });
    }
    return match != nullptr && focusPerson(match->personId());
}

void RelationGraphSceneController::setTraversalHighlight(PersonId personId,
                                                          PersonId previousPersonId) {
    if (!nodes_.contains(personId)) {
        clearTraversalHighlight();
        return;
    }
    if (scene_ != nullptr) {
        const QSignalBlocker blocker(scene_);
        scene_->clearSelection();
    }
    traversalPerson_ = personId;
    previousTraversalPerson_ = previousPersonId;
    applySelectionHighlight();
}

void RelationGraphSceneController::clearTraversalHighlight() {
    if (traversalPerson_ == PersonId{} &&
        previousTraversalPerson_ == PersonId{}) {
        return;
    }
    traversalPerson_ = {};
    previousTraversalPerson_ = {};
    applySelectionHighlight();
}

QGraphicsScene* RelationGraphSceneController::scene() const noexcept {
    return scene_;
}

PersonNodeItem* RelationGraphSceneController::nodeItem(PersonId personId) const noexcept {
    return nodes_.value(personId, nullptr);
}

RelationEdgeItem* RelationGraphSceneController::edgeItem(EdgeId edgeId) const noexcept {
    return edgesById_.value(edgeId, nullptr);
}

QList<PersonId> RelationGraphSceneController::personIds() const {
    auto result = nodes_.keys();
    std::sort(result.begin(), result.end());
    return result;
}

qsizetype RelationGraphSceneController::nodeCount() const noexcept {
    return nodes_.size();
}

qsizetype RelationGraphSceneController::edgeCount() const noexcept {
    return edges_.size();
}

std::uint64_t RelationGraphSceneController::revision() const noexcept {
    return revision_;
}

double RelationGraphSceneController::minimumJaccard() const noexcept {
    return minimumJaccard_;
}

QHash<PersonId, QPointF> RelationGraphSceneController::circularLayout(
    const std::vector<application::GraphNodeDto>& nodes) {
    std::vector<const application::GraphNodeDto*> sorted;
    sorted.reserve(nodes.size());
    for (const auto& node : nodes) {
        sorted.push_back(&node);
    }
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto* left, const auto* right) {
        if (left->id != right->id) {
            return left->id < right->id;
        }
        return left->canonicalName < right->canonicalName;
    });

    QHash<PersonId, QPointF> result;
    if (sorted.empty()) {
        return result;
    }
    if (sorted.size() == 1U) {
        result.insert(sorted.front()->id, QPointF{});
        return result;
    }

    const auto circumferenceRadius =
        static_cast<qreal>(sorted.size()) * 105.0 / (2.0 * kPi);
    const auto layoutRadius = std::max<qreal>(150.0, circumferenceRadius);
    for (std::size_t index = 0; index < sorted.size(); ++index) {
        const auto angle = -kPi / 2.0 +
                           2.0 * kPi * static_cast<qreal>(index) /
                               static_cast<qreal>(sorted.size());
        result.insert(sorted[index]->id,
                      QPointF{layoutRadius * std::cos(angle),
                              layoutRadius * std::sin(angle)});
    }
    return result;
}

void RelationGraphSceneController::handleSelectionChanged() {
    if (rebuilding_ || scene_ == nullptr || traversalPerson_ != PersonId{}) {
        return;
    }
    applySelectionHighlight();

    const auto selected = scene_->selectedItems();
    if (selected.isEmpty()) {
        emit selectionCleared();
        return;
    }
    auto* selectedItem = selected.front();
    if (selectedItem->type() == PersonNodeItem::Type) {
        auto* person = static_cast<PersonNodeItem*>(selectedItem);
        emit personSelected(person->personId());
    } else if (selectedItem->type() == RelationEdgeItem::Type) {
        auto* relation = static_cast<RelationEdgeItem*>(selectedItem);
        emit relationSelected(relation->edgeId(), relation->endpointA(),
                              relation->endpointB());
    }
}

void RelationGraphSceneController::applySelectionHighlight() {
    if (traversalPerson_ != PersonId{}) {
        for (auto* node : nodes_) {
            node->setVisualState(node->personId() == traversalPerson_
                                     ? PersonNodeItem::VisualState::Traversal
                                     : PersonNodeItem::VisualState::Dimmed);
        }
        for (auto* edge : edges_) {
            if (previousTraversalPerson_ != PersonId{} &&
                edge->connects(previousTraversalPerson_, traversalPerson_)) {
                edge->setVisualState(RelationEdgeItem::VisualState::Traversal);
            } else if (edge->connects(traversalPerson_)) {
                edge->setVisualState(RelationEdgeItem::VisualState::Adjacent);
                const auto adjacentId = edge->endpointA() == traversalPerson_
                                            ? edge->endpointB()
                                            : edge->endpointA();
                if (auto* adjacent = nodeItem(adjacentId); adjacent != nullptr) {
                    adjacent->setVisualState(PersonNodeItem::VisualState::Adjacent);
                }
            } else {
                edge->setVisualState(RelationEdgeItem::VisualState::Dimmed);
            }
        }
        return;
    }

    QGraphicsItem* selectedItem = nullptr;
    if (scene_ != nullptr && !scene_->selectedItems().isEmpty()) {
        selectedItem = scene_->selectedItems().front();
    }
    if (selectedItem == nullptr) {
        for (auto* node : nodes_) {
            node->setVisualState(PersonNodeItem::VisualState::Normal);
        }
        for (auto* edge : edges_) {
            edge->setVisualState(RelationEdgeItem::VisualState::Normal);
        }
        return;
    }

    for (auto* node : nodes_) {
        node->setVisualState(PersonNodeItem::VisualState::Dimmed);
    }
    for (auto* edge : edges_) {
        edge->setVisualState(RelationEdgeItem::VisualState::Dimmed);
    }

    if (selectedItem->type() == PersonNodeItem::Type) {
        auto* selectedPerson = static_cast<PersonNodeItem*>(selectedItem);
        selectedPerson->setVisualState(PersonNodeItem::VisualState::Selected);
        for (auto* edge : edges_) {
            if (!edge->connects(selectedPerson->personId())) {
                continue;
            }
            edge->setVisualState(RelationEdgeItem::VisualState::Adjacent);
            const auto adjacentId = edge->endpointA() == selectedPerson->personId()
                                        ? edge->endpointB()
                                        : edge->endpointA();
            if (auto* adjacent = nodeItem(adjacentId); adjacent != nullptr) {
                adjacent->setVisualState(PersonNodeItem::VisualState::Adjacent);
            }
        }
    } else if (selectedItem->type() == RelationEdgeItem::Type) {
        auto* selectedRelation = static_cast<RelationEdgeItem*>(selectedItem);
        selectedRelation->setVisualState(RelationEdgeItem::VisualState::Selected);
        if (auto* endpoint = nodeItem(selectedRelation->endpointA()); endpoint != nullptr) {
            endpoint->setVisualState(PersonNodeItem::VisualState::Selected);
        }
        if (auto* endpoint = nodeItem(selectedRelation->endpointB()); endpoint != nullptr) {
            endpoint->setVisualState(PersonNodeItem::VisualState::Selected);
        }
    }
}

void RelationGraphSceneController::selectOnly(QGraphicsItem* item) {
    if (scene_ == nullptr || item == nullptr) {
        return;
    }
    scene_->clearSelection();
    item->setSelected(true);
}

qreal RelationGraphSceneController::nodeRadius(std::uint32_t chapterCount,
                                               std::uint32_t minimum,
                                               std::uint32_t maximum) noexcept {
    if (minimum >= maximum) {
        return 27.0;
    }
    const auto normalized = static_cast<qreal>(chapterCount - minimum) /
                            static_cast<qreal>(maximum - minimum);
    return 22.0 + std::sqrt(std::clamp(normalized, 0.0, 1.0)) * 16.0;
}

}  // namespace novel::presentation
