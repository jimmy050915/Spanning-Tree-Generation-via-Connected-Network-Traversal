#pragma once

#include "presentation/models/UiRows.h"

#include <QAbstractTableModel>

#include <vector>

namespace novel::presentation {

class RelationTableModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        FirstPersonColumn,
        SecondPersonColumn,
        CoChapterCountColumn,
        JaccardColumn,
        ColumnCount
    };
    enum Role {
        EdgeIdRole = Qt::UserRole + 1,
        FirstPersonIdRole,
        SecondPersonIdRole,
        NumericSortRole
    };

    explicit RelationTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index,
                  int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setRows(std::vector<RelationRow> rows);
    const RelationRow* rowAt(int row) const noexcept;
    int rowForRelation(PersonId first, PersonId second) const noexcept;

private:
    std::vector<RelationRow> rows_;
};

}  // namespace novel::presentation
