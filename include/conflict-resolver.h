#pragma once

#include "utils.h"

class ConflictResolver {
public:
    static std::vector<std::unique_ptr<Change>>
        resolve(std::unique_ptr<Change> existing, std::unique_ptr<Change> incoming);
};