#pragma once

#include <gtest/gtest.h>
#include "database.h"
#include <nlohmann/json.hpp>
#include "logger.h"

using json = nlohmann::json;

class DatabaseUnitTest : public ::testing::Test {
protected:
    void SetUp() override {
        db = std::make_unique<Database>(std::string{ ":memory:" });
    }

    std::unique_ptr<Database> db;
};