#pragma once

#include "presentation/models/UiRows.h"

#include <QAbstractTableModel>

#include <vector>

namespace novel::presentation {

class PersonTableModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        IdColumn,
        NameColumn,
        ChapterCountColumn,
        DegreeColumn,
        StrongestPersonColumn,
        StrongestJaccardColumn,
        ColumnCount
    };
    enum Role { PersonIdRole = Qt::UserRole + 1 };

    explicit PersonTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index,
                  int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setRows(std::vector<PersonRow> rows);
    const PersonRow* rowAt(int row) const noexcept;
    int rowForPerson(PersonId id) const noexcept;

private:
    std::vector<PersonRow> rows_;
};

}  // namespace novel::presentation
