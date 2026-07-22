#pragma once

#include "domain/model/GraphTypes.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace novel::presentation {

struct PersonChoice {
    PersonId id{};
    QString name;
    quint32 chapterCount{};
};

struct AliasRow {
    QString alias;
    PersonId targetPerson{};
    QString targetName;
};

struct RecognizedMatch {
    QString matchedText;
    bool isAlias{};
    PersonId person{};
    QString canonicalName;
};

struct PendingAlias {
    QString alias;
    PersonId targetPerson{};
    QString targetNewPersonName;
};

struct ChapterEditorData {
    QString filePath;
    QString chapterKey;
    QString title;
    QString content;
    std::vector<RecognizedMatch> matches;
    std::vector<PersonId> selectedPeople;
    std::vector<PersonChoice> availablePeople;
};

struct ChapterEditorResult {
    QString chapterKey;
    QString title;
    QString content;
    std::vector<PersonId> selectedPeople;
    QStringList selectedNewPeople;
    std::vector<PendingAlias> newAliases;
};

}  // namespace novel::presentation
