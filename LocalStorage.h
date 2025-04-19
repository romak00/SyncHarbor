#pragma once

#include "BaseStorage.h"

class LocalStorage : public BaseStorage {
public:
    LocalStorage(const std::filesystem::path& home_dir, const int cloud_id, const std::string& db_file_name);
    ~LocalStorage() = default;

    void setupUploadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const override {}
    void setupUpdateHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileModifiedDTO>& dto) const override {}
    void setupDownloadHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileRecordDTO>& dto) const override {}
    void setupDeleteHandle(const std::unique_ptr<RequestHandle>& handle, const std::unique_ptr<FileDeletedDTO>& dto) const override {}

    std::vector<std::unique_ptr<ChangeDTO>> initialFiles() override {
        std::vector<std::unique_ptr<ChangeDTO>> dummy;
        return dummy;
    }

    std::vector<std::unique_ptr<ChangeDTO>> scanForChanges(std::shared_ptr<Database> db) override {
        std::vector<std::unique_ptr<ChangeDTO>> dummy;
        return dummy;
    }
    void proccesUpload(std::unique_ptr<FileRecordDTO>& dto, const std::string& response) const override {}

    void proccesUpdate(std::unique_ptr<FileModifiedDTO>& dto, const std::string& response) const override {}
    void proccesDownload(std::unique_ptr<FileRecordDTO>& dto, const std::string& response) const override {}
    void proccesDelete(std::unique_ptr<FileDeletedDTO>& dto, const std::string& response) const override {}

    const int id() const override {
        return _id;
    }

private:
    std::unique_ptr<Database> _db;
    std::filesystem::path _local_home_dir;
    int _id;
};
