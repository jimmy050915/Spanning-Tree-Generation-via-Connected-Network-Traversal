#pragma once

#include "domain/model/ChapterTypes.h"
#include "domain/model/GraphTypes.h"

#include <QString>

namespace novel::presentation {

struct PersonRow {
    PersonId id{};
    QString name;
    quint32 chapterCount{};
    quint32 degree{};
    PersonId strongestPersonId{};
    QString strongestPersonName;
    double strongestJaccard{};
};

struct RelationRow {
    EdgeId id{};
    PersonId firstId{};
    PersonId secondId{};
    QString firstName;
    QString secondName;
    quint32 coChapterCount{};
    double jaccard{};
};

struct ChapterRow {
    ChapterId id{};
    QString chapterKey;
    QString title;
    QString sourceFileName;
    quint32 personCount{};
    bool needsReview{};
};

}  // namespace novel::presentation
