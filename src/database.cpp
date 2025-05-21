#include "database.h"
#include "logger.h"

struct WhereTag {};
struct SetTag {};

Database::Database(const std::string& db_file) {
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    int rc = sqlite3_open_v2(db_file.c_str(), &_db, flags, nullptr);

    if (rc != SQLITE_OK) {
        std::string errMsg = _db ? sqlite3_errmsg(_db) : "Unknown error";
        throw std::runtime_error("Error opening db: " + errMsg);
    }

    execute("PRAGMA foreign_keys = ON;");
    execute("PRAGMA journal_mode = WAL;");
    execute("PRAGMA synchronous = NORMAL;");
    create_tables();
}

Database::~Database() {
    if (_db) {
        sqlite3_close(_db);
    }
}

int Database::add_cloud(
    const std::string& name,
    const CloudProviderType type,
    const nlohmann::json& config_data)
{
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = "INSERT INTO cloud_configs (name, type, config_data) "
        "VALUES (?, ?, ?)";

    int rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::string err = sqlite3_errmsg(_db);
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to prepare cloud_configs: " + err);
    }
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, to_cstr(type), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, config_data.dump().c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(_db);
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to insert new cloud: " + name + ", error msg:" + err);
    }
    int cloud_id = sqlite3_last_insert_rowid(_db);

    sqlite3_finalize(stmt);
    return cloud_id;
}

nlohmann::json Database::get_cloud_config(const int cloud_id) {
    sqlite3_stmt* stmt;
    const std::string sql = "SELECT config_data FROM cloud_configs "
        "WHERE config_id = ?";

    int rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to prepare SQL statement get_cloud_config");
    }

    sqlite3_bind_int64(stmt, 1, cloud_id);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Cloud not found");
    }

    std::string str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));

    sqlite3_finalize(stmt);

    return nlohmann::json::parse(str);
}

std::vector<nlohmann::json> Database::get_clouds() {
    sqlite3_stmt* stmt;
    const std::string sql = "SELECT config_id, type, config_data FROM cloud_configs;";

    int rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to prepare SQL statement get_clouds");
    }

    std::vector<nlohmann::json> clouds{};

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        nlohmann::json json_cloud;
        json_cloud["config_id"] = sqlite3_column_int(stmt, 0);
        std::string type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        json_cloud["type"] = type;
        std::string data = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        auto json_data = nlohmann::json::parse(data);
        json_cloud["config_data"] = json_data;
        clouds.emplace_back(std::move(json_cloud));
    }

    sqlite3_finalize(stmt);

    return clouds;
}
int Database::getGlobalIdByFileId(const uint64_t file_id) {
    LOG_DEBUG("Database", "Trying to global id for file_id: %i", file_id);

    sqlite3_stmt* stmt = nullptr;
    int rc = 0;

    std::string sql = "SELECT global_id FROM files WHERE file_id = ? LIMIT 1;";
    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to prepare statement get_cloud_file_info)");
    }

    sqlite3_bind_int64(stmt, 1, file_id);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("No file found for given file_id: " + std::to_string(file_id));

    }

    int global_id = sqlite3_column_int64(stmt, 0);

    sqlite3_finalize(stmt);

    return global_id;
}

int Database::getGlobalIdByPath(const std::filesystem::path& path) {
    sqlite3_stmt* stmt = nullptr;
    int rc = 0;

    std::string sql = "SELECT global_id FROM files WHERE path = ? LIMIT 1;";
    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to prepare statement getGlobalIdByPath)");
    }

    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("No file found for given path: " + path.string());
    }

    int global_id = sqlite3_column_int64(stmt, 0);

    sqlite3_finalize(stmt);

    return global_id;
}

std::unique_ptr<FileRecordDTO> Database::getFileByFileId(const uint64_t file_id) {
    sqlite3_stmt* stmt = nullptr;
    int rc = 0;

    std::string sql = "SELECT global_id, type, path, size, local_hash, local_modified_time FROM files WHERE file_id = ?;";
    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to prepare statement get_cloud_file_info)");
    }

    sqlite3_bind_int64(stmt, 1, file_id);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        LOG_ERROR("Database", "No file found for given file_id: %i", file_id);
        return nullptr;
    }

    int global_id = sqlite3_column_int64(stmt, 0);
    EntryType type = entry_type_from_string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
    std::string path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    sqlite3_int64 raw_size = sqlite3_column_int64(stmt, 3);
    uint64_t size = static_cast<uint64_t>(raw_size);
    sqlite3_int64 raw_hash = sqlite3_column_int64(stmt, 4);
    uint64_t local_hash = static_cast<uint64_t>(raw_hash);
    uint64_t local_modified_time = sqlite3_column_int64(stmt, 5);

    sqlite3_finalize(stmt);

    return std::make_unique<FileRecordDTO>(
        global_id,
        type,
        path,
        size,
        local_hash,
        local_modified_time,
        file_id
    );
}

std::unique_ptr<FileRecordDTO> Database::getFileByPath(const std::filesystem::path& path) {
    sqlite3_stmt* stmt = nullptr;
    int rc = 0;

    std::string sql = "SELECT global_id, type, file_id, size, local_hash, local_modified_time FROM files WHERE path = ?;";
    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to prepare statement getFileByPath)");
    }

    sqlite3_bind_text(stmt, 1, path.c_str(), -1 , SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        LOG_ERROR("Database", "No file found for given path: %s", path.c_str());
        return nullptr;
    }

    int global_id = sqlite3_column_int64(stmt, 0);
    EntryType type = entry_type_from_string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
    sqlite3_int64 raw_file_id = sqlite3_column_int64(stmt, 2);
    uint64_t file_id = static_cast<uint64_t>(raw_file_id);
    sqlite3_int64 raw_size = sqlite3_column_int64(stmt, 3);
    uint64_t size = static_cast<uint64_t>(raw_size);
    sqlite3_int64 raw_hash = sqlite3_column_int64(stmt, 4);
    uint64_t local_hash = static_cast<uint64_t>(raw_hash);
    uint64_t local_modified_time = sqlite3_column_int64(stmt, 5);

    sqlite3_finalize(stmt);

    return std::make_unique<FileRecordDTO>(
        global_id,
        type,
        path,
        size,
        local_hash,
        local_modified_time,
        file_id
    );
}

std::unique_ptr<FileRecordDTO> Database::getFileByGlobalId(const int global_id) {
    sqlite3_stmt* stmt = nullptr;
    int rc = 0;

    std::string sql = "SELECT file_id, type, path, size, local_hash, local_modified_time FROM files WHERE global_id = ?;";
    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to prepare statement get_cloud_file_info)");
    }

    sqlite3_bind_int64(stmt, 1, global_id);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        LOG_ERROR("Database", "No file found for given global_id: %i", global_id);
        return nullptr;
    }

    sqlite3_int64 raw_file_id = sqlite3_column_int64(stmt, 0);
    uint64_t file_id = static_cast<uint64_t>(raw_file_id);
    EntryType type = entry_type_from_string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
    std::string path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    sqlite3_int64 raw_size = sqlite3_column_int64(stmt, 3);
    uint64_t size = static_cast<uint64_t>(raw_size);
    sqlite3_int64 raw_hash = sqlite3_column_int64(stmt, 4);
    uint64_t local_hash = static_cast<uint64_t>(raw_hash);
    uint64_t local_modified_time = sqlite3_column_int64(stmt, 5);

    sqlite3_finalize(stmt);

    return std::make_unique<FileRecordDTO>(
        global_id,
        type,
        path,
        size,
        local_hash,
        local_modified_time,
        file_id
    );
}

std::filesystem::path Database::getMissingPathPart(const std::filesystem::path& path, const int num_clouds) {
    auto norm = normalizePath(path);

    LOG_DEBUG("Database", "getMissingPathPart() for path=%s and norm=%s", path.c_str(), norm.c_str());

    sqlite3_stmt* stmt = nullptr;
    int rc = 0;

    std::filesystem::path accum{};
    for (const auto& seq : norm) {
        accum /= seq;

        LOG_DEBUG("Database", "Checking existence of: %s", accum.c_str());

        std::string sql = "SELECT global_id FROM files WHERE path = ?;";
        rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            sqlite3_finalize(stmt);
            throw std::runtime_error("Failed to prepare statement (checkExistanceByPath)");
        }

        sqlite3_bind_text(stmt, 1, accum.c_str(), -1, SQLITE_STATIC);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_ROW) {
            sqlite3_finalize(stmt);
            auto parent = accum.parent_path();
            if (parent.empty()) {
                return norm;
            }
            auto missing = norm.lexically_relative(parent);
            LOG_DEBUG("Database", "Missing path part detected: %s", missing.c_str());
            return missing;
        }
        else {
            int global_id = sqlite3_column_int64(stmt, 0);
            sqlite3_finalize(stmt);

            sql = "SELECT cloud_file_id FROM file_links WHERE global_id = ?;";
            rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
            if (rc != SQLITE_OK) {
                sqlite3_finalize(stmt);
                throw std::runtime_error("Failed to prepare statement (getCloudFileIdbyPath)");
            }

            sqlite3_bind_int64(stmt, 1, global_id);

            int row_count = 0;
            while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
                ++row_count;
            }
            sqlite3_finalize(stmt);

            if (row_count != num_clouds) {
                auto parent = accum.parent_path();
                if (parent.empty()) {
                    return norm;
                }
                auto missing = norm.lexically_relative(parent);
                LOG_DEBUG("Database", "Missing path part detected: %s", missing.c_str());
                return missing;
            }
        }
    }

    LOG_DEBUG("Database", "No missing path parts for path=%s", path.string().c_str());
    return std::filesystem::path{};
}

std::string Database::getCloudFileIdByPath(const std::filesystem::path& path, const int cloud_id) {
    sqlite3_stmt* stmt = nullptr;
    int rc = 0;

    std::string sql = "SELECT global_id FROM files WHERE path = ?;";

    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to prepare statement getCloudFileIdbyPath)");
    }

    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        LOG_ERROR("Database", "No such global_id getCloudFileIdbyPath for path: %s", path.string());
        return std::string{};
    }

    int global_id = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);

    stmt = nullptr;
    sql = "SELECT cloud_file_id FROM file_links WHERE global_id = ? AND cloud_id = ?;";

    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to prepare statement getCloudFileIdbyPath)");
    }

    sqlite3_bind_int64(stmt, 1, global_id);
    sqlite3_bind_int64(stmt, 2, cloud_id);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        LOG_ERROR("Database", "No such global_id getCloudFileIdbyPath for path: %s and cloud: %s", path.string(), CloudResolver::getName(cloud_id));
        return std::string{};
    }

    std::string cloud_file_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    sqlite3_finalize(stmt);

    return cloud_file_id;

}

std::string Database::getParentId(const int cloud_id, const int global_id) {
    sqlite3_stmt* stmt = nullptr;
    int rc = 0;

    std::string sql = "SELECT cloud_parent_id FROM file_links WHERE cloud_id = ? AND global_id = ?;";

    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to prepare statement getParentId)");
    }

    sqlite3_bind_int64(stmt, 1, cloud_id);
    sqlite3_bind_int64(stmt, 2, global_id);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("No such global and cloud id getParentId: " + std::to_string(global_id) + std::to_string(cloud_id));
    }

    std::string parent_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    sqlite3_finalize(stmt);

    return parent_id;
}

int Database::add_file(const FileRecordDTO& dto) {
    sqlite3_busy_timeout(_db, 5000);
    LOG_DEBUG("Database", "Trying to add file: path: %s, file_id: %i", dto.rel_path.string(), dto.file_id);
    int rc = 0;
    rc = sqlite3_exec(_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    if (rc == SQLITE_BUSY) {
        std::cerr << "Ошибка: база данных занята (SQLITE_BUSY)" << std::endl;
    }
    else if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to begin transaction add_file");
    }
    sqlite3_stmt* stmt = nullptr;

    const std::string sql = "INSERT OR IGNORE INTO files (type, path, size, local_hash, local_modified_time, file_id) VALUES (?, ?, ?, ?, ?, ?);";
    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Failed to prepare SQL statement add_file");
    }
    sqlite3_bind_text(stmt, 1, to_cstr(dto.type), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, dto.rel_path.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, dto.size);
    sqlite3_bind_int64(stmt, 4, std::get<uint64_t>(dto.cloud_hash_check_sum));
    sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(dto.cloud_file_modified_time));
    sqlite3_bind_int64(stmt, 6, dto.file_id);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_BUSY) {
        std::cerr << "Ошибка: база данных занята (SQLITE_BUSY)" << std::endl;
    }
    else if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error adding file to files" + dto.rel_path.string());
    }
    int global_id = sqlite3_last_insert_rowid(_db);
    sqlite3_finalize(stmt);
    rc = sqlite3_exec(_db, "COMMIT;", nullptr, nullptr, nullptr);
    if (rc == SQLITE_BUSY) {
        std::cerr << "Ошибка: база данных занята (SQLITE_BUSY)" << std::endl;
    }
    else if (rc != SQLITE_OK) {
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error commiting file " + dto.rel_path.string());
    }
    return global_id;
}

bool Database::quickPathCheck(const std::filesystem::path& path) {
    sqlite3_stmt* stmt = nullptr;
    int rc = 0;

    std::string sql = "SELECT global_id FROM files WHERE path = ?;";
    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to prepare statement (quickPathCheck)");
    }

    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        LOG_DEBUG("Database", "Missing path detected: %s", path.c_str());
        return false;
    }
    else {
        sqlite3_finalize(stmt);
        LOG_DEBUG("Database", "Path exists: %s", path.c_str());
        return true;
    }
}

std::string Database::get_cloud_type(const int cloud_id) {
    sqlite3_stmt* stmt;
    const std::string sql = "SELECT type FROM cloud_configs WHERE config_id = ? LIMIT 1;";

    int rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to prepare SQL statement get_cloud_type");
    }
    sqlite3_bind_int64(stmt, 1, cloud_id);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("No file_link found for given cloud_file_id and cloud_id");
    }

    std::string type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    sqlite3_finalize(stmt);

    return type;
}

void Database::update_cloud_data(const int cloud_id, const nlohmann::json& data) {
    sqlite3_busy_timeout(_db, 5000);
    int rc = 0;
    rc = sqlite3_exec(_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    if (rc == SQLITE_BUSY) {
        std::cerr << "Ошибка: база данных занята (SQLITE_BUSY)" << std::endl;
    }
    else if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to begin transaction update_cloud_data");
    }
    sqlite3_stmt* stmt = nullptr;

    const std::string sql = "UPDATE cloud_configs SET config_data = ? WHERE config_id = ?;";
    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Failed to prepare SQL statement update_cloud_data");
    }

    sqlite3_bind_text(stmt, 1, data.dump().c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, cloud_id);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_BUSY) {
        std::cerr << "Ошибка: база данных занята (SQLITE_BUSY)" << std::endl;
    }
    else if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error changing update_cloud_data");
    }
    sqlite3_finalize(stmt);

    rc = sqlite3_exec(_db, "COMMIT;", nullptr, nullptr, nullptr);
    if (rc == SQLITE_BUSY) {
        std::cerr << "Ошибка: база данных занята (SQLITE_BUSY)" << std::endl;
    }
    else if (rc != SQLITE_OK) {
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error commiting change update_cloud_data");
    }
}

std::filesystem::path Database::getPathByGlobalId(const int search_global_id) {
    sqlite3_stmt* stmt = nullptr;
    int rc = 0;

    std::string sql = "SELECT path FROM files WHERE global_id = ? LIMIT 1;";
    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to prepare statement (file_links lookup)");
    }
    sqlite3_bind_int64(stmt, 1, search_global_id);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("No path found for given cloud_file_id and cloud_id");
    }
    std::string path_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    sqlite3_finalize(stmt);

    return std::filesystem::path(path_text);
}

std::string Database::get_cloud_parent_id_by_cloud_id(const int cloud_id, const std::string& cloud_file_id) {
    sqlite3_stmt* stmt = nullptr;
    int rc = 0;

    std::string sql = "SELECT cloud_parent_id FROM file_links WHERE cloud_id = ? AND cloud_file_id = ? LIMIT 1;";
    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to prepare statement (file_links lookup)");
    }
    sqlite3_bind_int64(stmt, 1, cloud_id);
    sqlite3_bind_text(stmt, 2, cloud_file_id.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("No file_link found for given cloud_id: " + std::to_string(cloud_id));
    }
    std::string cloud_parent_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));

    sqlite3_finalize(stmt);

    return cloud_parent_id;
}

std::unique_ptr<FileRecordDTO> Database::getFileByCloudIdAndCloudFileId(const int cloud_id, const std::string& cloud_file_id) {
    sqlite3_stmt* stmt = nullptr;
    int rc = 0;

    std::string sql = "SELECT global_id, cloud_parent_id, cloud_hash_check_sum, cloud_file_modified_time, cloud_size FROM file_links WHERE cloud_id = ? AND cloud_file_id = ? LIMIT 1;";
    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to prepare statement get_global_id");
    }
    sqlite3_bind_int64(stmt, 1, cloud_id);
    sqlite3_bind_text(stmt, 2, cloud_file_id.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        LOG_WARNING("DATABASE", "No link found for pair cloud_id: %i and cloud_file_id: %s", cloud_id, cloud_file_id);
        return nullptr;
    }
    int global_id = sqlite3_column_int64(stmt, 0);
    std::string cloud_parent_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    std::string cloud_hash_check_sum = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    time_t cloud_file_modified_time = sqlite3_column_int64(stmt, 3);
    sqlite3_int64 raw_size = sqlite3_column_int64(stmt, 4);
    uint64_t cloud_size = static_cast<uint64_t>(raw_size);

    sqlite3_finalize(stmt);

    return std::make_unique<FileRecordDTO>(
        global_id,
        cloud_id,
        cloud_parent_id,
        cloud_file_id,
        cloud_size,
        cloud_hash_check_sum,
        cloud_file_modified_time
    );
}

std::unique_ptr<FileRecordDTO> Database::getFileByCloudIdAndGlobalId(const int cloud_id, const int global_id) {
    sqlite3_stmt* stmt = nullptr;
    int rc = 0;

    std::string sql = "SELECT cloud_file_id, cloud_parent_id, cloud_hash_check_sum, cloud_file_modified_time, cloud_size FROM file_links WHERE cloud_id = ? AND global_id = ? LIMIT 1;";
    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to prepare statement getFileByCloudIdAndGlobalId");
    }
    sqlite3_bind_int64(stmt, 1, cloud_id);
    sqlite3_bind_int64(stmt, 2, global_id);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        LOG_WARNING("DATABASE", "No link found for pair cloud_id: %i and cloud_file_id: %i", cloud_id, global_id);
        return nullptr;
    }
    std::string cloud_file_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    std::string cloud_parent_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    std::string cloud_hash_check_sum = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    time_t cloud_file_modified_time = sqlite3_column_int64(stmt, 3);
    sqlite3_int64 raw_size = sqlite3_column_int64(stmt, 4);
    uint64_t cloud_size = static_cast<uint64_t>(raw_size);

    sqlite3_finalize(stmt);

    return std::make_unique<FileRecordDTO>(
        global_id,
        cloud_id,
        cloud_parent_id,
        cloud_file_id,
        cloud_size,
        cloud_hash_check_sum,
        cloud_file_modified_time
    );
}

std::string Database::get_cloud_file_id_by_cloud_id(const int cloud_id, const int global_id) {
    sqlite3_stmt* stmt = nullptr;
    int rc = 0;

    std::string sql = "SELECT cloud_file_id FROM file_links WHERE cloud_id = ? AND global_id = ? LIMIT 1;";
    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to prepare statement (file_links lookup)");
    }
    sqlite3_bind_int64(stmt, 1, cloud_id);
    sqlite3_bind_int64(stmt, 2, global_id);
    std::cout << "global_id" << global_id << '\n';

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("ERROR No file_link found for given cloud_id and global id: " + std::to_string(cloud_id));
    }
    std::string cloud_file_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));

    sqlite3_finalize(stmt);

    return cloud_file_id;
}

void Database::update_file_link(const FileUpdatedDTO& dto) {
    sqlite3_busy_timeout(_db, 5000);

    int rc = 0;
    rc = sqlite3_exec(_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to begin transaction update_file_link");
    }
    sqlite3_stmt* stmt = nullptr;

    const std::string sql = "UPDATE file_links SET cloud_hash_check_sum = ?, cloud_file_modified_time = ?, cloud_size = ? WHERE cloud_id = ? AND global_id = ?;";
    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Failed to prepare SQL statement update_file_link");
    }

    sqlite3_bind_text(stmt, 1, (std::get<std::string>(dto.cloud_hash_check_sum)).c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, dto.cloud_file_modified_time);
    sqlite3_bind_int64(stmt, 3, dto.size);
    sqlite3_bind_int(stmt, 4, dto.cloud_id);
    sqlite3_bind_int(stmt, 5, dto.global_id);

    rc = sqlite3_step(stmt);

    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error changing update_file_link");
    }
    sqlite3_finalize(stmt);

    rc = sqlite3_exec(_db, "COMMIT;", nullptr, nullptr, nullptr);

    if (rc != SQLITE_OK) {
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error commiting change update_file_link");
    }
}

void Database::update_file(const FileUpdatedDTO& dto) {
    sqlite3_busy_timeout(_db, 5000);

    int rc = 0;
    rc = sqlite3_exec(_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to begin transaction update_file_link");
    }
    sqlite3_stmt* stmt = nullptr;

    const std::string sql = "UPDATE files SET size = ?, local_hash = ?, local_modified_time = ?, file_id = ? WHERE global_id = ?;";
    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Failed to prepare SQL statement update_file_link");
    }

    sqlite3_bind_int64(stmt, 1, dto.size);
    sqlite3_bind_int64(stmt, 2, std::get<uint64_t>(dto.cloud_hash_check_sum));
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(dto.cloud_file_modified_time));
    sqlite3_bind_int64(stmt, 4, dto.file_id);
    sqlite3_bind_int(stmt, 5, dto.global_id);

    rc = sqlite3_step(stmt);

    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error changing update_file_link");
    }
    sqlite3_finalize(stmt);

    rc = sqlite3_exec(_db, "COMMIT;", nullptr, nullptr, nullptr);

    if (rc != SQLITE_OK) {
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error commiting change update_file_link");
    }
}

void Database::update_file_link(const FileMovedDTO& dto) {
    sqlite3_busy_timeout(_db, 5000);

    int rc = 0;
    rc = sqlite3_exec(_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to begin transaction update_file_link");
    }
    sqlite3_stmt* stmt = nullptr;

    const std::string sql = "UPDATE file_links SET cloud_file_id = ?, cloud_file_modified_time = ?, cloud_parent_id = ? WHERE cloud_id = ? AND global_id = ?;";
    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Failed to prepare SQL statement update_file_link");
    }

    sqlite3_bind_text(stmt, 1, dto.cloud_file_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, dto.cloud_file_modified_time);
    sqlite3_bind_text(stmt, 3, dto.new_cloud_parent_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 4, dto.cloud_id);
    sqlite3_bind_int(stmt, 5, dto.global_id);

    rc = sqlite3_step(stmt);

    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error changing update_file_link");
    }
    sqlite3_finalize(stmt);

    rc = sqlite3_exec(_db, "COMMIT;", nullptr, nullptr, nullptr);

    if (rc != SQLITE_OK) {
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error commiting change update_file_link");
    }
}

void Database::update_file(const FileMovedDTO& dto) {
    sqlite3_busy_timeout(_db, 5000);

    int rc = 0;
    rc = sqlite3_exec(_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to begin transaction update_file_link");
    }
    sqlite3_stmt* stmt = nullptr;

    const std::string sql = "UPDATE files SET path = ?, local_modified_time = ? WHERE global_id = ?;";
    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Failed to prepare SQL statement update_file_link");
    }

    sqlite3_bind_text(stmt, 1, dto.new_rel_path.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(dto.cloud_file_modified_time));
    sqlite3_bind_int(stmt, 3, dto.global_id);

    rc = sqlite3_step(stmt);

    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error changing update_file_link");
    }
    sqlite3_finalize(stmt);

    rc = sqlite3_exec(_db, "COMMIT;", nullptr, nullptr, nullptr);

    if (rc != SQLITE_OK) {
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error commiting change update_file_link");
    }
}

void Database::delete_file_and_links(const int global_id) {
    sqlite3_busy_timeout(_db, 5000);

    int rc = 0;
    rc = sqlite3_exec(_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to begin transaction delete_file");
    }
    sqlite3_stmt* stmt = nullptr;

    const std::string sql = "DELETE FROM files WHERE global_id = ?;";

    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Failed to prepare SQL statement delete_file");
    }
    sqlite3_bind_int64(stmt, 1, global_id);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error deleting from files: " + std::to_string(global_id));
    }

    sqlite3_finalize(stmt);
    rc = sqlite3_exec(_db, "COMMIT;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error commiting deletion: " + std::to_string(global_id));
    }
}

void Database::add_file_link(const FileRecordDTO& dto)
{
    sqlite3_busy_timeout(_db, 5000);
    int rc = 0;
    rc = sqlite3_exec(_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    if (rc == SQLITE_BUSY) {
        std::cerr << "Ошибка: база данных занята (SQLITE_BUSY)" << std::endl;
    }
    else if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to begin transaction add_file_links");
    }
    sqlite3_stmt* stmt = nullptr;
    const std::string sql = "INSERT OR IGNORE INTO file_links (global_id, cloud_id, cloud_file_id, cloud_parent_id, cloud_file_modified_time, cloud_hash_check_sum, cloud_size) VALUES (?, ?, ?, ?, ?, ?, ?);";
    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Failed to prepare SQL statement add_file_links");
    }

    sqlite3_bind_int64(stmt, 1, dto.global_id);
    sqlite3_bind_int(stmt, 2, dto.cloud_id);
    sqlite3_bind_text(stmt, 3, dto.cloud_file_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, dto.cloud_parent_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 5, dto.cloud_file_modified_time);
    sqlite3_bind_text(stmt, 6, (std::get<std::string>(dto.cloud_hash_check_sum)).c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 7, dto.size);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_BUSY) {
        std::cerr << "Ошибка: база данных занята (SQLITE_BUSY)" << std::endl;
    }
    else if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error adding file to file_links " + std::to_string(dto.global_id));
    }

    sqlite3_finalize(stmt);
    rc = sqlite3_exec(_db, "COMMIT;", nullptr, nullptr, nullptr);
    if (rc == SQLITE_BUSY) {
        std::cerr << "Ошибка: база данных занята (SQLITE_BUSY)" << std::endl;
    }
    else if (rc != SQLITE_OK) {
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error commiting file_link " + std::to_string(dto.global_id));
    }
}

bool Database::isInitialSyncDone() {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT value FROM metadata WHERE name = 'initial_sync_done';";
    sqlite3_prepare_v2(_db, sql, -1, &stmt, nullptr);
    bool done = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        done = std::string(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0))) == "1";
    }
    sqlite3_finalize(stmt);
    return done;
}

void Database::markInitialSyncDone() {
    sqlite3_busy_timeout(_db, 5000);
    int rc = 0;
    rc = sqlite3_exec(_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    if (rc == SQLITE_BUSY) {
        std::cerr << "Ошибка: база данных занята (SQLITE_BUSY)" << std::endl;
    }
    else if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to begin transaction markInitialSyncDone");
    }

    sqlite3_stmt* stmt = nullptr;
    const std::string sql = "INSERT OR REPLACE INTO metadata(name, value) VALUES('initial_sync_done','1');";
    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Failed to prepare SQL statement markInitialSyncDone");
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_BUSY) {
        std::cerr << "Ошибка: база данных занята (SQLITE_BUSY)" << std::endl;
    }
    else if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error doing markInitialSyncDone");
    }

    sqlite3_finalize(stmt);
    rc = sqlite3_exec(_db, "COMMIT;", nullptr, nullptr, nullptr);
    if (rc == SQLITE_BUSY) {
        std::cerr << "Ошибка: база данных занята (SQLITE_BUSY)" << std::endl;
    }
    else if (rc != SQLITE_OK) {
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error commiting markInitialSyncDone");
    }
}

void Database::execute(const std::string& sql) {
    char* err = nullptr;
    if (sqlite3_exec(_db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK) {
        std::string error = "SQL error: " + std::string(err);
        sqlite3_free(err);
        throw std::runtime_error(error);
    }
}

void Database::check_rc(int rc, const std::string& context) {
    if (rc != SQLITE_OK) {
        throw std::runtime_error(context + ": " + sqlite3_errmsg(_db));
    }
}

void Database::create_tables() {
    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS metadata (
            name  TEXT PRIMARY KEY,
            value TEXT NOT NULL
        );
        CREATE TABLE IF NOT EXISTS cloud_configs (
            config_id   INTEGER PRIMARY KEY AUTOINCREMENT,
            name        TEXT UNIQUE NOT NULL,
            type        TEXT NOT NULL,
            config_data TEXT NOT NULL
        );
        CREATE TABLE IF NOT EXISTS files (
            global_id           INTEGER PRIMARY KEY AUTOINCREMENT,
            type                TEXT NOT NULL,
            path                TEXT NOT NULL,
            size                INTEGER,
            local_hash          INTEGER,
            local_modified_time INTEGER,
            file_id             INTEGER NOT NULL
        );
        CREATE TABLE IF NOT EXISTS file_links (
            global_id                 INTEGER NOT NULL,
            cloud_id                  INTEGER NOT NULL,
            cloud_file_id             TEXT NOT NULL,
            cloud_parent_id           TEXT NOT NULL,
            cloud_file_modified_time  INTEGER,
            cloud_hash_check_sum      TEXT,
            cloud_size                INTEGER,
            PRIMARY KEY (global_id, cloud_id),
            FOREIGN KEY(global_id) REFERENCES files(global_id) ON DELETE CASCADE,
            FOREIGN KEY(cloud_id) REFERENCES cloud_configs(config_id) ON DELETE CASCADE
        );
    )";
    const char* indexes_sql = R"(
        CREATE INDEX IF NOT EXISTS idx_files_path ON files(path);
        CREATE INDEX IF NOT EXISTS idx_files_id ON files(file_id);
        CREATE INDEX IF NOT EXISTS idx_file_links_cloud_file_id_cloud_id ON file_links(cloud_file_id, cloud_id);
        CREATE INDEX IF NOT EXISTS idx_file_links_global_cloud ON file_links(global_id, cloud_id);

    )";

    char* err = nullptr;
    int rc = sqlite3_exec(_db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string error = "SQL error: " + std::string(err);
        sqlite3_free(err);
        throw std::runtime_error(error);
    }
    rc = sqlite3_exec(_db, indexes_sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string error = "SQL error: " + std::string(err);
        sqlite3_free(err);
        throw std::runtime_error(error);
    }
}