#include <iostream>
#include <string>
#include <sstream>
#include <iomanip>
#include <vector>
#include <queue>
#include <unordered_map>
#include <filesystem>
#include <random>

#include "utils.h"
#include "CallbackDispatcher.h"
#include "database.h"
#include "Networking.h"
#include "google.h"
#include "dropbox.h"
#include "LocalStorage.h"
#include "logger.h"



int main() {

    ThreadNamer::setThreadName("main");
    Logger::get().setConsoleLogging(false);
    Logger::get().setGlobalLogLevel(LogLevel::DEBUG);

    std::filesystem::path dir("/home/rk00/demo");
    std::string config_name = "conf1";
    std::string db_file_name = "conf1.sqlite3";

    auto db = std::make_unique<Database>(db_file_name);
    std::ifstream config_file(config_name);
    std::stringstream buf;
    buf << config_file.rdbuf();
    const auto config_json = nlohmann::json::parse(buf.str());

    const auto ftime = std::filesystem::last_write_time(dir);
    const auto stime = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
    const auto time = std::chrono::system_clock::to_time_t(stime);

    auto loc_dir_fr = std::make_unique<FileRecordDTO>(
        EntryType::Directory,
        std::filesystem::path(""),
        0,
        time,
        0
    );
    
    int global_id = db->add_file(loc_dir_fr);

    std::unordered_map<int, std::shared_ptr<BaseStorage>> clouds;

    for (const auto& cloud : config_json["clouds"]) {
        std::string name = cloud["name"];
        std::string type = cloud["type"];
        const nlohmann::json cloud_data = cloud["data"];
        const std::filesystem::path cloud_home_path(cloud_data["dir"]);
        int cloud_id = db->add_cloud(name, type, cloud_data);
        if (type == "GoogleDrive") {
            clouds.emplace(
                cloud_id,
                std::make_shared<GoogleDrive>(
                    cloud_data["client_id"],
                    cloud_data["client_secret"],
                    cloud_data["refresh_token"],
                    cloud_home_path,
                    dir,
                    cloud_id));
            CloudResolver::registerCloud(cloud_id, "GoogleDrive");
        }
        else if (type == "Dropbox") {
            clouds.emplace(
                cloud_id,
                std::make_shared<Dropbox>(
                    cloud_data["client_id"],
                    cloud_data["client_secret"],
                    cloud_data["refresh_token"],
                    cloud_home_path,
                    dir,
                    cloud_id));
            CloudResolver::registerCloud(cloud_id, "Dropbox");
        }
    }

    clouds.emplace(0, std::make_shared<LocalStorage>(dir, 0, db_file_name));
    CloudResolver::registerCloud(0, "LocalStorage");

    HttpClient::get().setClouds(clouds);
    CallbackDispatcher::get().setDB(db_file_name);
    CallbackDispatcher::get().setClouds(clouds);

    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
        const auto entry_path = entry.path();
        const auto ftime = std::filesystem::last_write_time(entry_path);
        const auto stime = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
        const auto time = std::chrono::system_clock::to_time_t(stime);
        if (entry.is_regular_file()) {
            auto fr = std::make_unique<FileRecordDTO>(
                EntryType::File,
                std::filesystem::relative(entry.path(), dir),
                std::filesystem::file_size(entry_path),
                time,
                0
            );

            auto local_upload = std::make_unique<LocalUploadCommand>(0);
            local_upload->setDTO(std::move(fr));
            for (const auto& [cloud_id, cloud] : clouds) {
                if (cloud_id != 0) {
                    auto cloud_upload = std::make_unique<CloudUploadCommand>(cloud_id);
                    local_upload->addNext(std::move(cloud_upload));
                }
            }

            CallbackDispatcher::get().submit(std::move(local_upload));
        }
    }

    HttpClient::get().shutdown();







    return 0;
}