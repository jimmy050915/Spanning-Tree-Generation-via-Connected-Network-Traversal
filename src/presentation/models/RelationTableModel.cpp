#include "presentation/models/RelationTableModel.h"

#include <algorithm>

namespace novel::presentation {

RelationTableModel::RelationTableModel(QObject* parent)
    : QAbstractTableModel(parent) {}

int RelationTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int RelationTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant RelationTableModel::data(const QModelIndex& index, int role) const {
    const auto* row = rowAt(index.row());
    if (!row || index.column() < 0 || index.column() >= ColumnCount) {
        return {};
    }
    if (role == EdgeIdRole) {
        return QVariant::fromValue(row->id);
    }
    if (role == FirstPersonIdRole) {
        return QVariant::fromValue(row->firstId);
    }
    if (role == SecondPersonIdRole) {
        return QVariant::fromValue(row->secondId);
    }
    if (role == NumericSortRole) {
        if (index.column() == CoChapterCountColumn) {
            return row->coChapterCount;
        }
        if (index.column() == JaccardColumn) {
            return row->jaccard;
        }
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole) {
        return {};
    }
    switch (index.column()) {
        case FirstPersonColumn:
            return row->firstName;
        case SecondPersonColumn:
            return row->secondName;
        case CoChapterCountColumn:
            return row->coChapterCount;
        case JaccardColumn:
            return QString::number(row->jaccard, 'f', 4);
        default:
            return {};
    }
}

QVariant RelationTableModel::headerData(int section,
                                        Qt::Orientation orientation,
                                        int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    switch (section) {
        case FirstPersonColumn:
            return tr("人物 A");
        case SecondPersonColumn:
            return tr("人物 B");
        case CoChapterCountColumn:
            return tr("共同章节数");
        case JaccardColumn:
            return tr("Jaccard 关联度");
        default:
            return {};
    }
}

void RelationTableModel::setRows(std::vector<RelationRow> rows) {
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

const RelationRow* RelationTableModel::rowAt(int row) const noexcept {
    if (row < 0 || static_cast<std::size_t>(row) >= rows_.size()) {
        return nullptr;
    }
    return &rows_[static_cast<std::size_t>(row)];
}

int RelationTableModel::rowForRelation(PersonId first,
                                       PersonId second) const noexcept {
    const auto found = std::find_if(
        rows_.begin(), rows_.end(), [first, second](const RelationRow& row) {
            return (row.firstId == first && row.secondId == second) ||
                   (row.firstId == second && row.secondId == first);
        });
    return found == rows_.end()
               ? -1
               : static_cast<int>(std::distance(rows_.begin(), found));
}

}  // namespace novel::presentation
