#include "presentation/models/ChapterTableModel.h"

#include <algorithm>

namespace novel::presentation {

ChapterTableModel::ChapterTableModel(QObject* parent)
    : QAbstractTableModel(parent) {}

int ChapterTableModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

int ChapterTableModel::columnCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant ChapterTableModel::data(const QModelIndex& index, int role) const {
    const auto* row = rowAt(index.row());
    if (!row || index.column() < 0 || index.column() >= ColumnCount) {
        return {};
    }
    if (role == ChapterIdRole) {
        return QVariant::fromValue(row->id);
    }
    if (role == SourceFileRole) {
        return row->sourceFileName;
    }
    if (role != Qt::DisplayRole && role != Qt::EditRole) {
        return {};
    }
    switch (index.column()) {
        case KeyColumn:
            return row->chapterKey;
        case TitleColumn:
            return row->title;
        case PersonCountColumn:
            return row->personCount;
        case SourceFileColumn:
            return row->sourceFileName;
        case StatusColumn:
            return row->needsReview ? tr("待检查") : tr("正常");
        default:
            return {};
    }
}

QVariant ChapterTableModel::headerData(int section,
                                       Qt::Orientation orientation,
                                       int role) const {
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    switch (section) {
        case KeyColumn:
            return tr("章节编号");
        case TitleColumn:
            return tr("标题");
        case PersonCountColumn:
            return tr("人物数");
        case SourceFileColumn:
            return tr("来源文件");
        case StatusColumn:
            return tr("状态");
        default:
            return {};
    }
}

void ChapterTableModel::setRows(std::vector<ChapterRow> rows) {
    beginResetModel();
    rows_ = std::move(rows);
    endResetModel();
}

const ChapterRow* ChapterTableModel::rowAt(int row) const noexcept {
    if (row < 0 || static_cast<std::size_t>(row) >= rows_.size()) {
        return nullptr;
    }
    return &rows_[static_cast<std::size_t>(row)];
}

int ChapterTableModel::rowForChapter(ChapterId id) const noexcept {
    const auto found = std::find_if(rows_.begin(), rows_.end(),
                                    [id](const ChapterRow& row) {
                                        return row.id == id;
                                    });
    return found == rows_.end()
               ? -1
               : static_cast<int>(std::distance(rows_.begin(), found));
}

}  // namespace novel::presentation
