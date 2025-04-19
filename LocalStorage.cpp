#include "LocalStorage.h"

LocalStorage::LocalStorage(const std::filesystem::path& home_dir, const int cloud_id, const std::string& db_file_name) {
    _local_home_dir = home_dir;
    _id = cloud_id;
    _db = std::make_unique<Database>(db_file_name);
}