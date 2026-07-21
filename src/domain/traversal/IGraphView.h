#pragma once

#include "domain/model/GraphTypes.h"

#include <vector>

namespace novel {

class IGraphView {
public:
    virtual ~IGraphView() = default;

    // Returns every real person exactly once, ordered by ascending PersonId.
    virtual std::vector<PersonId> vertices() const = 0;
    // Returns every adjacent person exactly once, ordered by ascending PersonId.
    virtual std::vector<PersonId> neighbors(PersonId id) const = 0;
};

}  // namespace novel
