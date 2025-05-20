#include "sync-manager.h"
#include "cloud-registry.h"
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <limits>
#include <cctype>

auto listConfigs(const std::string& dir = ".") {
    std::vector<std::string> configs;
    for (auto& p : std::filesystem::directory_iterator(dir)) {
        if (p.path().extension() == ".sqlite3") {
            configs.push_back(p.path().stem().string());
        }
    }
    std::sort(configs.begin(), configs.end());
    return configs;
}

static bool askYesNo(const std::string& question, bool defaultYes = true) {
    char c;
    std::string hint = defaultYes ? "[Y/n]" : "[y/N]";
    for (;;) {
        std::cout << question << " " << hint << ": ";
        if (!(std::cin >> c)) {
            continue;
        }
        c = std::tolower(c);
        if (c == 'y' || c == 'n') {
            return c == 'y';
        }
    }
}

static std::string readLine(const std::string& prompt) {
    std::cout << prompt;
    std::string s;
    std::getline(std::cin >> std::ws, s);
    return s;
}

static std::string chooseOrCreateConfig() {
    auto configs = listConfigs();
    if (configs.empty()) {
        std::cout << "No existing configs found. Please create one.\n";
    }
    else {
        std::cout << "Existing configs:" << std::endl;
        for (size_t i = 0; i < configs.size(); ++i) {
            std::cout << "  " << (i + 1) << ") " << configs[i] << std::endl;
        }
        std::cout << "  0) Create a new config" << std::endl;
        std::cout << "Select config number (or 0 to create new): ";
        int choice;
        if (std::cin >> choice && choice > 0 && choice <= (int)configs.size()) {
            return configs[choice - 1];
        }
    }
    std::string name;
    std::cout << "Enter new config name: ";
    std::cin >> name;
    return name;
}

void configureLocalDirectory(const std::unique_ptr<SyncManager>& sync_manager) {
    while (true) {
        auto local = readLine("Enter local folder to sync: ");
        try {
            sync_manager->setLocalDir(local);
            auto bad = sync_manager->checkLocalPermissions();
            if (!bad.empty()) {
                std::cout << "Warning! These files/folders will be ignored (no access):\n";
                for (auto& p : bad) {
                    std::cout << "  " << p << "\n";
                }
            }
            std::cout << "Local folder is ready.\n\n";
            break;
        }
        catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n";
            if (!askYesNo("Choose another folder?", false)) {
                std::exit(1);
            } 
        }
    }
}

void configureClouds(const std::unique_ptr<SyncManager>& sync_manager) {
    std::cout << "=== Cloud Setup Wizard ===\n";
    while (true) {
        std::vector<std::string> keys;
        int idx = 1;
        std::cout << "Available cloud providers:\n";
        for (auto& [key, info] : CloudRegistry) {
            std::cout << "  " << idx << ") " << key << "\n";
            keys.push_back(key);
            ++idx;
        }
        std::cout << "Select provider (0 to finish): ";
        int choice;
        std::cin >> choice;
        if (choice <= 0 || choice > (int)keys.size()) {
            break;
        }
        auto provider = keys[choice - 1];
        auto const& info = CloudRegistry.at(provider);

        std::string cid = info.default_client_id;
        if (!cid.empty() && !askYesNo("Use default client_id?")) {
            cid = readLine("Enter client_id: ");
        }
        std::string csec = info.default_client_secret;
        if (!csec.empty() && !askYesNo("Use default client_secret?")) {
            csec = readLine("Enter client_secret: ");
        }

        std::string cloud_dir = readLine("Enter cloud root path (e.g. /path/to/dir) [default '/']: ");
        if (cloud_dir.empty()) {
            cloud_dir = "/";
        }

        std::string cloud_name = readLine("Enter cloud name (e.g. google1, my-dropbox): [default 'my-cloud']: ");
        if (cloud_name.empty()) {
            cloud_name = "my-cloud";
        }

        try {
            sync_manager->registerCloud(cloud_name, info.type, cid, csec, cloud_dir);
            /* auto bad = sync_manager->checkCloudPermissions(provider);
            if (!bad.empty()) {
                std::cout << "Warning! These cloud entries will be ignored (no access):\n";
                for (auto& e : bad) std::cout << "  " << e << "\n";
            }
            auto free_space = sync_manager->getCloudFreeSpace(provider); */
            //std::cout << "Free space on cloud: " << free_space << " bytes\n";
        }
        catch (const std::exception& e) {
            std::cout << "Error configuring " << provider << ": " << e.what() << "\n";
            if (!askYesNo("Retry this provider?", false)) {
                continue;
            }
        }
        if (!askYesNo("Add another cloud?", false)) {
            break;
        }
    }
    std::cout << "Cloud configuration complete.\n\n";
}

static std::atomic<bool> g_should_exit{ false };

void on_sigint(int)
{
    g_should_exit.store(true);
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, on_sigint);

    ThreadNamer::setThreadName("main");
    Logger::get().setConsoleLogging(false);
    Logger::get().setGlobalLogLevel(LogLevel::DEBUG);

    if (argc < 2) {
        std::cout << "Usage:\n"
            << "  " << argv[0] << " config [<configName>]\n"
            << "  " << argv[0] << " run-daemon [<configName>]\n";
        return 1;
    }

    std::string mode = argv[1];
    std::string config_name;
    if (argc >= 3) {
        config_name = argv[2];
    }

    std::unique_ptr<SyncManager> sync_manager = nullptr;

    if (mode == "config") {
        if (config_name.empty()) {
            config_name = chooseOrCreateConfig();
        }
        std::cout << ">> Initial sync for config: " << config_name << "\n";
        std::string db_file = config_name + ".sqlite3";
        sync_manager = std::make_unique<SyncManager>(db_file, SyncManager::Mode::InitialSync);
        configureLocalDirectory(sync_manager);
        configureClouds(sync_manager);
        std::cout << "Starting initial sync...\n";
        sync_manager->run();
        return 0;
    }
    else if (mode == "run-daemon") {
        if (config_name.empty()) {
            auto configs = listConfigs();
            if (configs.empty()) {
                std::cout << "No configs exist; run `config` first.\n";
                return 1;
            }
            std::cout << "Select config:\n";
            for (size_t i = 0; i < configs.size(); ++i) {
                std::cout << "  " << (i + 1) << ") " << configs[i] << "\n";
            }
            int choice; std::cin >> choice;
            if (choice < 1 || choice >(int)configs.size()) return 1;
            config_name = configs[choice - 1];
        }
        std::string db_file = config_name + ".sqlite3";
        std::cout << ">> Running daemon for config: " << config_name << "\n";
        sync_manager = std::make_unique<SyncManager>(db_file, SyncManager::Mode::Daemon);
        sync_manager->run();
        return 0;
    }
    else if (mode == "config-file-initial") {
        if (config_name.empty()) {
            std::cerr << "Usage: " << argv[0] << " config-file <cfg.json>\n";
            return 1;
        }
        sync_manager =
            std::make_unique<SyncManager>(config_name, config_name + ".sqlite3", "/home/rk00/demo", SyncManager::Mode::InitialSync);

        sync_manager->run();
        sync_manager->shutdown();
        return 0;
    }
    else if (mode == "config-file-daemon") {
        if (config_name.empty()) {
            std::cerr << "Usage: " << argv[0] << " config-file <cfg.json>\n";
            return 1;
        }
        sync_manager =
            std::make_unique<SyncManager>(config_name, config_name + ".sqlite3", "/home/rk00/demo", SyncManager::Mode::Daemon);

        sync_manager->run();
    }
    else {
        std::cerr << "Unknown mode: " << mode << "\n";
        return 1;
    }

    LOG_DEBUG("Main", "Waiting for Ctrl+C....");
    while (!g_should_exit.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    if (sync_manager) {
        sync_manager->shutdown();
    }
    LOG_DEBUG("Main", "Recieved SIGINGT. Shutting down...");
}
