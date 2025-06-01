#pragma once

#include <gtest/gtest.h>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "LocalStorage.h"
#include "database.h"
#include "CallbackDispatcher.h"

class LocalStorageUnitTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto d = std::filesystem::temp_directory_path() / "-test-local_storage-";
        std::error_code ec;
        std::filesystem::remove_all(d, ec);
        std::filesystem::create_directory(d);
        tmp = std::filesystem::canonical(d);

        db = std::make_shared<Database>(std::string{ ":memory:" });

        CallbackDispatcher::get().setDB(db);

        cid = db->add_cloud("unit", CloudProviderType::Dropbox, nlohmann::json::object());
        CloudResolver::registerCloud(cid, "unit");

        ls = std::make_unique<LocalStorage>(tmp, cid, db);
        ls->setOnChange([] {});
    }

    void TearDown() override {
        std::filesystem::remove_all(tmp);
    }

    std::filesystem::path              tmp;
    std::shared_ptr<Database>          db;
    std::unique_ptr<LocalStorage>      ls;
    int                                cid;
};