#include "presentation/models/SearchSortProxyModel.h"

#include <QAbstractItemModel>

namespace novel::presentation {

SearchSortProxyModel::SearchSortProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent) {
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    setSortCaseSensitivity(Qt::CaseInsensitive);
    setDynamicSortFilter(true);
}

bool SearchSortProxyModel::filterAcceptsRow(
    int sourceRow, const QModelIndex& sourceParent) const {
    if (filterRegularExpression().pattern().isEmpty()) {
        return true;
    }
    const auto* source = sourceModel();
    if (!source) {
        return false;
    }
    for (int column = 0; column < source->columnCount(sourceParent); ++column) {
        const auto text = source
                              ->data(source->index(sourceRow, column,
                                                  sourceParent),
                                     filterRole())
                              .toString();
        if (text.contains(filterRegularExpression())) {
            return true;
        }
    }
    return false;
}

bool SearchSortProxyModel::lessThan(const QModelIndex& sourceLeft,
                                    const QModelIndex& sourceRight) const {
    const auto left = sourceModel()->data(sourceLeft, sortRole());
    const auto right = sourceModel()->data(sourceRight, sortRole());
    if (left == right) {
        return sourceLeft.row() < sourceRight.row();
    }
    return QSortFilterProxyModel::lessThan(sourceLeft, sourceRight);
}

}  // namespace novel::presentation
