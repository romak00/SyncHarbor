#include <iostream>
#include "database.h"

struct WhereTag {};
struct SetTag {};


Database::Database(const std::string& db_file) {
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX;
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

int Database::add_cloud(const std::string& name,
    const std::string& type,
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
    sqlite3_bind_text(stmt, 2, type.c_str(), -1, SQLITE_STATIC);
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

    const char* str = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    auto json_str = nlohmann::json::parse(str);

    sqlite3_finalize(stmt);

    return std::move(json_str);
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
        const std::string type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        json_cloud["type"] = type;
        const std::string data = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        auto json_data = nlohmann::json::parse(data);
        json_cloud["config_data"] = json_data;
        clouds.emplace_back(std::move(json_cloud));
    }

    sqlite3_finalize(stmt);

    return std::move(clouds);
}

int Database::add_file(const std::unique_ptr<FileRecordDTO>& dto) {
    sqlite3_busy_timeout(_db, 5000);
    int rc = 0;
    rc = sqlite3_exec(_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    if (rc == SQLITE_BUSY) {
        std::cerr << "Ошибка: база данных занята (SQLITE_BUSY)" << std::endl;
    }
    else if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to begin transaction add_file");
    }
    sqlite3_stmt* stmt = nullptr;

    const std::string sql = "INSERT INTO files (type, path, size, local_hash, local_modified_time) VALUES (?, ?, ?, ?, ?);";
    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Failed to prepare SQL statement add_file");
    }
    sqlite3_bind_text(stmt, 1, to_cstr(dto->type), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, dto->rel_path.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(dto->size));
    sqlite3_bind_int64(stmt, 4, atoi((dto->cloud_hash_check_sum).c_str()));
    sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(dto->cloud_file_modified_time));

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_BUSY) {
        std::cerr << "Ошибка: база данных занята (SQLITE_BUSY)" << std::endl;
    }
    else if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error adding file to files" + dto->rel_path.string());
    }
    int global_id = sqlite3_last_insert_rowid(_db);
    sqlite3_finalize(stmt);
    rc = sqlite3_exec(_db, "COMMIT;", nullptr, nullptr, nullptr);
    if (rc == SQLITE_BUSY) {
        std::cerr << "Ошибка: база данных занята (SQLITE_BUSY)" << std::endl;
    }
    else if (rc != SQLITE_OK) {
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error commiting file " + dto->rel_path.string());
    }
    return global_id;
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

std::filesystem::path Database::find_path_by_global_id(const int search_global_id) {
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
        throw std::runtime_error("No file_link found for given cloud_file_id and cloud_id");
    }
    std::string path_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    std::filesystem::path path(path_text);
    sqlite3_finalize(stmt);

    return path;
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
        throw std::runtime_error("No file_link found for given cloud_id: " + cloud_id);
    }
    std::string cloud_parent_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));

    sqlite3_finalize(stmt);

    return std::move(cloud_parent_id);
}

int Database::get_global_id_by_cloud_id(const int cloud_id, const std::string& cloud_file_id) {
    sqlite3_stmt* stmt = nullptr;
    int rc = 0;

    std::string sql = "SELECT global_id FROM file_links WHERE cloud_id = ? AND cloud_file_id = ? LIMIT 1;";
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
        throw std::runtime_error("get_global_id error: No file_link found for given cloud_id: " + cloud_id);
    }
    int global_id = sqlite3_column_int64(stmt, 0);

    sqlite3_finalize(stmt);

    return global_id;
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
        throw std::runtime_error("ERROR No file_link found for given cloud_id and global id: " + cloud_id);
    }
    std::string cloud_file_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));

    sqlite3_finalize(stmt);

    return std::move(cloud_file_id);
}

void Database::update_file_link_one_field(const int cloud_id, const int global_id, const std::string& field, const std::string& new_str) {
    sqlite3_busy_timeout(_db, 5000);

    int rc = 0;
    rc = sqlite3_exec(_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to begin transaction update_file_link_data");
    }
    sqlite3_stmt* stmt = nullptr;

    const std::string sql = "UPDATE file_links SET " + field + " = ? WHERE cloud_id = ? AND global_id = ?;";
    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Failed to prepare SQL statement update_file_link_one_field");
    }

    sqlite3_bind_text(stmt, 1, new_str.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, cloud_id);
    sqlite3_bind_int64(stmt, 3, global_id);

    rc = sqlite3_step(stmt);

    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error changing update_file_link_one_field");
    }
    sqlite3_finalize(stmt);

    rc = sqlite3_exec(_db, "COMMIT;", nullptr, nullptr, nullptr);

    if (rc != SQLITE_OK) {
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error commiting change update_file_link_one_field");
    }
}

void Database::update_file_link(const int cloud_id, const int global_id, const std::string& hash, const int mod_time, const std::string& parent_id, const std::string& cloud_file_id) {
    sqlite3_busy_timeout(_db, 5000);

    int rc = 0;
    rc = sqlite3_exec(_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to begin transaction update_file_link");
    }
    sqlite3_stmt* stmt = nullptr;

    const std::string sql = "UPDATE file_links SET cloud_hash_check_sum = ?, cloud_file_modified_time = ?, cloud_parent_id = ?, cloud_file_id = ? WHERE cloud_id = ? AND global_id = ?;";
    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Failed to prepare SQL statement update_file_link");
    }

    sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, mod_time);
    sqlite3_bind_text(stmt, 3, parent_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, cloud_file_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 5, cloud_id);
    sqlite3_bind_int(stmt, 6, global_id);

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

void Database::update_file_time(const int global_id, const std::string& field, const std::time_t& time) {
    sqlite3_busy_timeout(_db, 5000);

    int rc = 0;
    rc = sqlite3_exec(_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to begin transaction update_file_time");
    }
    sqlite3_stmt* stmt = nullptr;

    const std::string sql = "UPDATE files SET " + field + " = ? WHERE global_id = ?;";
    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Failed to prepare SQL statement update_file_time");
    }

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(time));
    sqlite3_bind_int64(stmt, 2, global_id);

    rc = sqlite3_step(stmt);

    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error changing update_file_time");
    }
    sqlite3_finalize(stmt);

    rc = sqlite3_exec(_db, "COMMIT;", nullptr, nullptr, nullptr);

    if (rc != SQLITE_OK) {
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error commiting change update_file_time");
    }
}

void Database::update_file_path(const int global_id, const std::string& field, const std::string& path) {
    sqlite3_busy_timeout(_db, 5000);

    int rc = 0;
    rc = sqlite3_exec(_db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to begin transaction update_file_path");
    }
    sqlite3_stmt* stmt = nullptr;

    const std::string sql = "UPDATE files SET " + field + " = ? WHERE global_id = ?;";
    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Failed to prepare SQL statement update_file_path");
    }

    sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, global_id);

    rc = sqlite3_step(stmt);

    if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error changing update_file_path");
    }
    sqlite3_finalize(stmt);

    rc = sqlite3_exec(_db, "COMMIT;", nullptr, nullptr, nullptr);

    if (rc != SQLITE_OK) {
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error commiting change update_file_path");
    }
}

nlohmann::json Database::get_cloud_file_info(const std::string& cloud_file_id, const int cloud_id) {
    sqlite3_stmt* stmt = nullptr;
    int rc = 0;

    std::string sql = "SELECT global_id, cloud_parent_id, cloud_file_modified_time, cloud_hash_check_sum FROM file_links WHERE cloud_file_id = ? AND cloud_id = ? LIMIT 1;";
    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        throw std::runtime_error("Failed to prepare statement get_cloud_file_info)");
    }
    sqlite3_bind_text(stmt, 1, cloud_file_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 2, cloud_id);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        std::cout << "No file_link found for given cloud_id and cloud_file_id: " << cloud_id << " " << cloud_file_id << '\n';
        return nlohmann::json::object();
    }
    nlohmann::json cloud_info;
    cloud_info["global_id"] = sqlite3_column_int64(stmt, 0);
    const std::string parent = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    cloud_info["cloud_parent_id"] = parent;
    cloud_info["cloud_file_modified_time"] = sqlite3_column_int64(stmt, 2);
    const std::string hash = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    cloud_info["cloud_hash_check_sum"] = hash;

    sqlite3_finalize(stmt);

    return std::move(cloud_info);
}

void Database::add_file_link(const std::unique_ptr<FileRecordDTO>& dto)
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
    const std::string sql = "INSERT INTO file_links (global_id, cloud_id, cloud_file_id, cloud_parent_id, cloud_file_modified_time, cloud_hash_check_sum) VALUES (?, ?, ?, ?, ?, ?);";
    rc = sqlite3_prepare_v2(_db, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Failed to prepare SQL statement add_file_links");
    }


    sqlite3_bind_int64(stmt, 1, dto->global_id);
    sqlite3_bind_int(stmt, 2, dto->cloud_id);
    sqlite3_bind_text(stmt, 3, dto->cloud_file_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, dto->cloud_parent_id.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 5, dto->cloud_file_modified_time);
    sqlite3_bind_text(stmt, 6, dto->cloud_hash_check_sum.c_str(), -1, SQLITE_STATIC);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_BUSY) {
        std::cerr << "Ошибка: база данных занята (SQLITE_BUSY)" << std::endl;
    }
    else if (rc != SQLITE_DONE) {
        sqlite3_finalize(stmt);
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error adding file to file_links " + dto->global_id);
    }

    sqlite3_finalize(stmt);
    rc = sqlite3_exec(_db, "COMMIT;", nullptr, nullptr, nullptr);
    if (rc == SQLITE_BUSY) {
        std::cerr << "Ошибка: база данных занята (SQLITE_BUSY)" << std::endl;
    }
    else if (rc != SQLITE_OK) {
        sqlite3_exec(_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        throw std::runtime_error("Error commiting file_link " + dto->global_id);
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
            local_modified_time INTEGER
        );
        CREATE TABLE IF NOT EXISTS file_links (
            global_id                 INTEGER NOT NULL,
            cloud_id                  INTEGER NOT NULL,
            cloud_file_id             TEXT NOT NULL,
            cloud_parent_id           TEXT NOT NULL,
            cloud_hash_check_sum      TEXT,
            cloud_file_modified_time  INTEGER,
            PRIMARY KEY (global_id, cloud_id),
            FOREIGN KEY(global_id) REFERENCES files(global_id) ON DELETE CASCADE,
            FOREIGN KEY(cloud_id) REFERENCES cloud_configs(config_id) ON DELETE CASCADE
        );
    )";
    const char* indexes_sql = R"(
        CREATE INDEX IF NOT EXISTS idx_files_path ON files(path);
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