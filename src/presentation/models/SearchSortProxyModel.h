#pragma once

#include <QSortFilterProxyModel>

namespace novel::presentation {

class SearchSortProxyModel final : public QSortFilterProxyModel {
    Q_OBJECT

public:
    explicit SearchSortProxyModel(QObject* parent = nullptr);

protected:
    bool filterAcceptsRow(int sourceRow,
                          const QModelIndex& sourceParent) const override;
    bool lessThan(const QModelIndex& sourceLeft,
                  const QModelIndex& sourceRight) const override;
};

}  // namespace novel::presentation
