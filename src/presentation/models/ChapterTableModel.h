#pragma once

#include "presentation/models/UiRows.h"

#include <QAbstractTableModel>

#include <vector>

namespace novel::presentation {

class ChapterTableModel final : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        KeyColumn,
        TitleColumn,
        PersonCountColumn,
        SourceFileColumn,
        StatusColumn,
        ColumnCount
    };
    enum Role { ChapterIdRole = Qt::UserRole + 1, SourceFileRole };

    explicit ChapterTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index,
                  int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;

    void setRows(std::vector<ChapterRow> rows);
    const ChapterRow* rowAt(int row) const noexcept;
    int rowForChapter(ChapterId id) const noexcept;

private:
    std::vector<ChapterRow> rows_;
};

}  // namespace novel::presentation
