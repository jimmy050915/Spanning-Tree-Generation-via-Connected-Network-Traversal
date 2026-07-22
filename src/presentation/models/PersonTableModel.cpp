#include "presentation/models/PersonTableModel.h"

#include <algorithm>

namespace novel::presentation {

PersonTableModel::PersonTableModel(QObject* parent)
    : QAbstractTableModel(parent) {}

int PersonTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int PersonTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant PersonTableModel::data(const QModelIndex& index, int role) const {
    const auto* row = rowAt(index.row());
    if (!row || index.column() < 0 || index.column() >= ColumnCount) {
        return {};
    }
    if (role == PersonIdRole) {
        return QVariant::fromValue(row->id);
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole) {
        return {};
    }
    switch (index.column()) {
        case IdColumn:
            return QVariant::fromValue(
                static_cast<qulonglong>(row->id));
        case NameColumn:
            return row->name;
        case ChapterCountColumn:
            return row->chapterCount;
        case DegreeColumn:
            return row->degree;
        case StrongestPersonColumn:
            return row->strongestPersonName;
        case StrongestJaccardColumn:
            return row->strongestPersonId == PersonId{}
                       ? QVariant{}
                       : QVariant(QString::number(row->strongestJaccard,
                                                  'f', 4));
        default:
            return {};
    }
}

QVariant PersonTableModel::headerData(int section,
                                      Qt::Orientation orientation,
                                      int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    switch (section) {
        case IdColumn:
            return tr("人物编号");
        case NameColumn:
            return tr("标准人物名");
        case ChapterCountColumn:
            return tr("出现章节数");
        case DegreeColumn:
            return tr("直接关联人数");
        case StrongestPersonColumn:
            return tr("最高关联人物");
        case StrongestJaccardColumn:
            return tr("最高关联度");
        default:
            return {};
    }
}

void PersonTableModel::setRows(std::vector<PersonRow> rows) {
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

const PersonRow* PersonTableModel::rowAt(int row) const noexcept {
    if (row < 0 || static_cast<std::size_t>(row) >= rows_.size()) {
        return nullptr;
    }
    return &rows_[static_cast<std::size_t>(row)];
}

int PersonTableModel::rowForPerson(PersonId id) const noexcept {
    const auto found = std::find_if(rows_.begin(), rows_.end(),
                                    [id](const PersonRow& row) {
                                        return row.id == id;
                                    });
    return found == rows_.end()
               ? -1
               : static_cast<int>(std::distance(rows_.begin(), found));
}

}  // namespace novel::presentation
