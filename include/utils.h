#pragma once

#include <string>
#include <filesystem>
#include <unordered_map>
#include <chrono>

std::filesystem::path normalizePath(const std::filesystem::path& p);

enum class CloudProviderType {
    LocalStorage,
    GoogleDrive,
    Dropbox,
    OneDrive,
    Yandex,
    MailRu,
    FakeTest
};

const char* to_cstr(CloudProviderType type);

std::string_view to_string(CloudProviderType type);

CloudProviderType cloud_type_from_string(std::string_view str);

std::time_t convertCloudTime(std::string datetime);

std::time_t convertSystemTime(const std::filesystem::path& p);

enum class EntryType : uint8_t {
    File = 0,
    Directory = 1,
    Document = 2,
    Null = 3
};

const char* to_cstr(EntryType type);

std::string_view to_string(EntryType type);

EntryType entry_type_from_string(std::string_view str);

class FileRecordDTO {
public:
    FileRecordDTO(                          // LocalStorage NEW
        const EntryType t,
        const std::filesystem::path& rp,
        const uint64_t s,
        const std::time_t cfmt,
        const uint64_t hash,
        const uint64_t fid
    ) :
        type(t),
        rel_path(rp),
        size(s),
        cloud_file_modified_time(cfmt),
        cloud_hash_check_sum(hash),
        file_id(fid),
        cloud_id(0),
        global_id(0)
    {
    }

    FileRecordDTO(                          // Cloud info from db
        const int g_id,
        const int c_id,
        const std::string& parent,
        const std::string& cf_id,
        const uint64_t s,
        const std::string& hash,
        const std::time_t cfmt
    ) :
        cloud_parent_id(parent),
        cloud_id(c_id),
        cloud_file_id(cf_id),
        size(s),
        cloud_file_modified_time(cfmt),
        cloud_hash_check_sum(hash),
        global_id(g_id),
        file_id(0)
    {
    }

    FileRecordDTO(                          // LocalStorage info from db
        const int g_id,
        const EntryType t,
        const std::filesystem::path& rp,
        const uint64_t s,
        const uint64_t hash,
        const std::time_t cfmt,
        const uint64_t fid
    ) :
        type(t),
        rel_path(rp),
        size(s),
        cloud_file_modified_time(cfmt),
        cloud_hash_check_sum(hash),
        file_id(fid),
        global_id(g_id),
        cloud_id(0)
    {
    }

    FileRecordDTO(                          // NOT GoogleDrive Cloud NEW info
        const EntryType t,
        const std::filesystem::path& rp,
        const std::string& cf_id,
        const uint64_t s,
        const std::time_t cfmt,
        const std::string& hash,
        const int cid
    ) :
        type(t),
        rel_path(rp),
        cloud_file_id(cf_id),
        size(s),
        cloud_file_modified_time(cfmt),
        cloud_hash_check_sum(hash),
        cloud_id(cid),
        file_id(0),
        global_id(0)
    {
    }

    FileRecordDTO(                          // GoogleDrive Cloud NEW info
        const EntryType t,
        const std::string& parent,
        const std::filesystem::path& rp,
        const std::string& cf_id,
        const uint64_t s,
        const std::time_t cfmt,
        const std::string& hash,
        const int cid
    ) :
        type(t),
        cloud_parent_id(parent),
        rel_path(rp),
        cloud_file_id(cf_id),
        size(s),
        cloud_file_modified_time(cfmt),
        cloud_hash_check_sum(hash),
        cloud_id(cid),
        file_id(0),
        global_id(0)
    {
    }


    FileRecordDTO() = default;
    ~FileRecordDTO() = default;

    FileRecordDTO(const FileRecordDTO& other) = default;
    FileRecordDTO(FileRecordDTO&& other) noexcept = default;

    FileRecordDTO& operator=(const FileRecordDTO& other) = default;
    FileRecordDTO& operator=(FileRecordDTO&& other) noexcept = default;

    std::filesystem::path rel_path;
    std::string cloud_parent_id;
    std::string cloud_file_id;
    std::variant<std::string, uint64_t> cloud_hash_check_sum;
    uint64_t size;
    uint64_t file_id;
    std::time_t cloud_file_modified_time;
    int global_id;
    int cloud_id;
    EntryType type;
};

enum class ChangeType : uint8_t {
    Null = 0,
    New = 1 << 0,
    Update = 1 << 1,
    Move = 1 << 2,
    Rename = 1 << 3,
    Delete = 1 << 4
};

std::string to_string(ChangeType ch);

class FileEvent {
public:
    FileEvent(
        const std::filesystem::path& p,
        const std::time_t tm,
        const ChangeType tp,
        std::shared_ptr<FileEvent> ass
    ) :
        path(p),
        when(tm),
        type(tp),
        associated(ass),
        file_id(0)
    {
    }

    FileEvent(
        const std::filesystem::path& p,
        const std::time_t tm,
        const ChangeType tp
    ) :
        path(p),
        when(tm),
        type(tp),
        associated(nullptr),
        file_id(0)
    {
    }

    FileEvent() = default;
    ~FileEvent() = default;
    FileEvent(const FileEvent& other) = default;
    FileEvent& operator=(const FileEvent& other) = default;

    FileEvent& operator=(FileEvent&& other) noexcept = default;
    FileEvent(FileEvent&& other) noexcept = default;

    std::filesystem::path path;
    std::shared_ptr<FileEvent> associated;
    uint64_t file_id;
    std::time_t when;
    ChangeType type;
};

class FileUpdatedDTO {
public:
    FileUpdatedDTO(                        // LocalStorage Modify
        const EntryType t,
        const int gid,
        const std::uint64_t chcs,
        const std::time_t cfmt,
        const std::filesystem::path& rp,
        const uint64_t s,
        const uint64_t fid
    ) :
        global_id(gid),
        cloud_hash_check_sum(chcs),
        rel_path(rp),
        cloud_file_modified_time(cfmt),
        type(t),
        size(s),
        file_id(fid),
        cloud_id(0)
    {
    }

    FileUpdatedDTO(                        // Cloud Modify
        const EntryType t,
        const int gid,
        const int cid,
        const std::string& cfid,
        const std::string& chcs,
        const std::time_t cfmt,
        const std::filesystem::path& rp,
        const std::string& cpid,
        const uint64_t s
    ) :
        global_id(gid),
        cloud_id(cid),
        cloud_file_id(cfid),
        cloud_hash_check_sum(chcs),
        cloud_parent_id(cpid),
        rel_path(rp),
        cloud_file_modified_time(cfmt),
        type(t),
        size(s),
        file_id(0)
    {
    }

    FileUpdatedDTO() = default;
    ~FileUpdatedDTO() = default;

    FileUpdatedDTO(const FileUpdatedDTO& other) = default;
    FileUpdatedDTO(FileUpdatedDTO&& other) noexcept = default;

    FileUpdatedDTO& operator=(const FileUpdatedDTO& other) = default;
    FileUpdatedDTO& operator=(FileUpdatedDTO&& other) noexcept = default;

    std::filesystem::path rel_path;
    std::string cloud_file_id;
    std::variant<std::string, uint64_t> cloud_hash_check_sum;
    std::string cloud_parent_id;
    uint64_t size;
    std::time_t cloud_file_modified_time;
    uint64_t file_id;
    int global_id;
    int cloud_id;
    EntryType type;
};

class FileMovedDTO {
public:
    FileMovedDTO(                        // LocalStorage Modify
        const EntryType t,
        const int gid,
        const std::time_t cfmt,
        const std::filesystem::path& orp,
        const std::filesystem::path& nrp
    ) :
        global_id(gid),
        old_rel_path(orp),
        new_rel_path(nrp),
        cloud_file_modified_time(cfmt),
        type(t),
        cloud_id(0)
    {
    }

    FileMovedDTO(                        // Cloud Modify
        const EntryType t,
        const int gid,
        const int cid,
        const std::string& cfid,
        const std::time_t cfmt,
        const std::filesystem::path& orp,
        const std::filesystem::path& nrp,
        const std::string& old_cpid,
        const std::string& new_cpid
    ) :
        global_id(gid),
        cloud_id(cid),
        cloud_file_id(cfid),
        old_cloud_parent_id(old_cpid),
        new_cloud_parent_id(new_cpid),
        old_rel_path(orp),
        new_rel_path(nrp),
        cloud_file_modified_time(cfmt),
        type(t)
    {
    }

    FileMovedDTO() = default;
    ~FileMovedDTO() = default;

    FileMovedDTO(const FileMovedDTO& other) = default;
    FileMovedDTO(FileMovedDTO&& other) noexcept = default;

    FileMovedDTO& operator=(const FileMovedDTO& other) = default;
    FileMovedDTO& operator=(FileMovedDTO&& other) noexcept = default;

    std::filesystem::path old_rel_path;
    std::filesystem::path new_rel_path;
    std::string cloud_file_id;
    std::string old_cloud_parent_id;
    std::string new_cloud_parent_id;
    std::time_t cloud_file_modified_time;
    int global_id;
    int cloud_id;
    EntryType type;
};

class FileDeletedDTO {
public:
    FileDeletedDTO(                        // LocalStorage Delete
        const std::filesystem::path& rp,
        const int gid,
        const std::time_t t
    ) :
        rel_path(rp),
        global_id(gid),
        when(t),
        cloud_id(0)
    {
    }

    FileDeletedDTO(                         // Cloud Delete
        const std::filesystem::path& rp,
        const int gid,
        const int cid,
        const std::string& cfid,
        const std::time_t t
    ) :
        rel_path(rp),
        global_id(gid),
        cloud_id(cid),
        cloud_file_id(cfid),
        when(t)
    {
    }

    FileDeletedDTO() = default;
    ~FileDeletedDTO() = default;

    FileDeletedDTO(const FileDeletedDTO& other) = default;
    FileDeletedDTO(FileDeletedDTO&& other) noexcept = default;

    FileDeletedDTO& operator=(const FileDeletedDTO& other) = default;
    FileDeletedDTO& operator=(FileDeletedDTO&& other) noexcept = default;

    std::filesystem::path rel_path;
    std::string cloud_file_id;
    std::time_t when;
    int global_id;
    int cloud_id;
};