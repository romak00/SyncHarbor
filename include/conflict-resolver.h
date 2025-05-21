#pragma once

#include "utils.h"

class ConflictResolver {
public:
    static std::vector<std::shared_ptr<Change>>
        resolve(std::shared_ptr<Change> existing, std::shared_ptr<Change> incoming);
};