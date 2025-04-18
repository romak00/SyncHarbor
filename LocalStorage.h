#pragma once

#include "BaseStorage.h"

class LocalStorage : BaseStorage {
public:
    LocalStorage(const std::filesystem::path& home_dir, const int cloud_id);
    ~LocalStorage() = default;

    void setupUploadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) override {}
    void setupUpdateHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileModifiedDTO>& dto) override {}
    void setupDownloadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) override {}
    void setupDeleteHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileDeletedDTO>& dto) override {}

    std::vector<std::unique_ptr<ChangeDTO>> initialFiles() override;

    std::vector<std::unique_ptr<ChangeDTO>> scanForChanges(std::shared_ptr<Database> db) override;
    void proccesUpload(std::unique_ptr<FileRecordDTO>& dto, const std::string& response) override;

    const int id() const override {
        return _id;
    }

private:
    std::unique_ptr<Database> _db;
    std::filesystem::path _local_home_dir;
    int _id;
};
