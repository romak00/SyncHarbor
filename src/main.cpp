#include "sync-manager.h"
#include "cloud-registry.h"
#include "daemonizer.h"
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <limits>
#include <cctype>
#include <csignal>

#ifdef _WIN32
#include <windows.h>
    using process_id_t = DWORD;
#else
#include <sys/types.h>
#include <signal.h>
#include <unistd.h>
    using process_id_t = pid_t;
#endif

static std::filesystem::path getBaseDataDir() {
    if (const char* env = std::getenv("SYNCHARBOR_DATA_DIR"); env && *env) {
        return std::filesystem::path(env);
    }
#if defined(__linux__) || defined(__APPLE__)
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg) {
        return std::filesystem::path(xdg) / "syncharbor";
    }
    if (const char* home = std::getenv("HOME"); home && *home) {
        return std::filesystem::path(home) / ".local" / "share" / "syncharbor";
    }
    return std::filesystem::current_path() / ".syncharbor";
#elif defined(_WIN32)
    if (const char* appdata = std::getenv("APPDATA"); appdata && *appdata) {
        return std::filesystem::path(appdata) / "SyncHarbor";
    }
    if (const char* home = std::getenv("USERPROFILE"); home && *home) {
        return std::filesystem::path(home) / "SyncHarbor";
    }
    return std::filesystem::current_path() / ".syncharbor";
#else
    return std::filesystem::current_path() / ".syncharbor";
#endif
}

static std::filesystem::path getDataDirForConfig(const std::string& config_name) {
    return getBaseDataDir() / config_name;
}


static std::vector<std::string> listConfigs() {
    std::vector<std::string> configs;
    std::filesystem::path base = getBaseDataDir();
    if (!std::filesystem::exists(base)) {
        return configs;
    }
    for (auto& p : std::filesystem::directory_iterator(base)) {
        if (!p.is_directory()) {
            continue;
        }
        configs.push_back(p.path().filename().string());
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

#if defined(_WIN32)
BOOL WINAPI ConsoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT || signal == CTRL_BREAK_EVENT) {
        g_should_exit.store(true, std::memory_order_release);
        return TRUE;
    }
    return FALSE;
}
#endif

static bool terminateByPid(process_id_t pid) {
#if defined(_WIN32)
    HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (!h) {
        return false;
    }
    BOOL ok = TerminateProcess(h, 1);
    CloseHandle(h);
    return ok == TRUE;
#else
    if (::kill(pid, SIGTERM) != 0) {
        return false;
    }
    return true;
#endif
}


int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
#else
    std::signal(SIGINT, on_sigint);
    std::signal(SIGTERM, on_sigint);
#endif

    ThreadNamer::setThreadName("main");
    Logger::get().setConsoleLogging(false);
    Logger::get().setGlobalLogLevel(LogLevel::DBG);

    if (argc < 2) {
        std::cout << "Usage:\n"
            << "  " << argv[0] << " --config [<configName>]\n"
            << "  " << argv[0] << " --run-daemon [<configName>]\n"
            << "  " << argv[0] << " --stop-daemon [<configName>]\n"
            << "  " << argv[0] << " --show-data-dir [<configName>]\n";
        return 1;
    }

    std::string mode = argv[1];
    std::string config_name;
    if (argc >= 3) {
        config_name = argv[2];
    }


    if (mode == "--stop-daemon") {
        if (config_name.empty()) {
            std::cerr << "Usage: " << argv[0] << " --stop-daemon <configName>\n";
            return 1;
        }
        auto data_dir = getDataDirForConfig(config_name);
        std::filesystem::path pid_file = data_dir / (config_name + ".pid");

        if (!std::filesystem::exists(pid_file)) {
            std::cerr << "Daemon for '" << config_name << "' not running (pid-file does not exists).\n";
            return 1;
        }

        std::ifstream in(pid_file);
        if (!in) {
            std::cerr << "Cannot open pid-file: " << pid_file << "\n";
            return 1;
        }
        process_id_t pid = 0;
        in >> pid;
        in.close();

        if (pid <= 0) {
            std::cerr << "Incorrect PID in " << pid_file << "\n";
            std::filesystem::remove(pid_file);
            return 1;
        }

        bool alive = false;
#if defined(_WIN32)
        HANDLE hcheck = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
        if (hcheck) {
            DWORD exit_code = 0;
            if (GetExitCodeProcess(hcheck, &exit_code)) {
                alive = (exit_code == STILL_ACTIVE);    
            }
            else {
                alive = false;
            }
            CloseHandle(hcheck);
        }
#else
        alive = (kill(pid, 0) == 0);
#endif

        if (!alive) {
            std::cerr << "Proccess " << pid << " already stopped. Deleting pid-file.\n";
            std::filesystem::remove(pid_file);
            return 1;
        }

        if (!terminateByPid(pid)) {
            std::perror("terminateByPid");
            std::cerr << "Could not send signal to the proccess " << pid << "\n";
            return 1;
        }
        std::cout << "Sending stop signal (pid=" << pid << "). Waiting...\n";

        for (int i = 0; i < 50; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
#if defined(_WIN32)
            HANDLE hup = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
            if (!hup) {
                break;
            }
            DWORD code = 0;
            GetExitCodeProcess(hup, &code);
            CloseHandle(hup);
            if (code != STILL_ACTIVE)
            {
                break;
            }
#else
            if (kill(pid, 0) != 0) {
                break;
            }
#endif
        }
        bool still_alive = false;
#if defined(_WIN32)
        HANDLE h2 = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
        if (h2) {
            DWORD code = 0;
            GetExitCodeProcess(h2, &code);
            CloseHandle(h2);
            still_alive = (code == STILL_ACTIVE);
        }
#else
        still_alive = (kill(pid, 0) == 0);
#endif

        if (still_alive) {
            std::cerr << "Daemon still running, sending hard SIGKILL/TerminateProcess.\n";
#if defined(_WIN32)
            HANDLE h3 = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
            if (h3) {
                TerminateProcess(h3, 1);
                CloseHandle(h3);
        }
#else
            ::kill(pid, SIGKILL);
#endif
    }
        std::error_code ec;
        std::filesystem::remove(pid_file, ec);
        if (ec) {
            std::cerr << "Warning: could not delete pid-file: " << pid_file << "\n";
        }

        std::cout << "Daemon for '" << config_name << "' stopped.\n";
        return 0;
    }


    if (mode == "--show-data-dir") {
        if (config_name.empty()) {
            config_name = "default";
        }
        auto data_dir = getDataDirForConfig(config_name);
        std::cout << data_dir << "\n";
        return 0;
    }

    std::unique_ptr<SyncManager> sync_manager = nullptr;

    if (mode == "--config") {
        signal(SIGINT, SIG_DFL);

        if (config_name.empty()) {
            config_name = chooseOrCreateConfig();
        }
        std::cout << ">> Initial sync for config: " << config_name << "\n";

        std::filesystem::path data_dir = getDataDirForConfig(config_name);
        std::filesystem::create_directories(data_dir);

#if CUSTOM_LOGGING_ENABLED
        try {
            std::filesystem::create_directories(data_dir / "logs");
            Logger::get().addLogFile("general", (data_dir / "logs" / "syncharbor-initial.log").string());
        }
        catch (const std::exception& e) {
            std::cerr << "Cannot create data folder: " << (data_dir / "logs") << ": " << e.what() << "\n";
            return 1;
        }
#endif

        std::string db_full_path = (data_dir / (config_name + ".sqlite3")).string();

        sync_manager = std::make_unique<SyncManager>(db_full_path, SyncManager::Mode::InitialSync);

        configureLocalDirectory(sync_manager);
        configureClouds(sync_manager);
        std::cout << "Starting initial sync...\n";

        signal(SIGINT, on_sigint);

        sync_manager->run();
        sync_manager->shutdown();
        return 0;
    }
    else if (mode == "--run-daemon") {
        if (config_name.empty()) {
            auto configs = listConfigs();
            if (configs.empty()) {
                std::cout << "No configs exist; run `--config` first.\n";
                return 1;
            }
            std::cout << "Select config:\n";
            for (size_t i = 0; i < configs.size(); ++i) {
                std::cout << "  " << (i + 1) << ") " << configs[i] << "\n";
            }
            int choice; std::cin >> choice;
            if (choice < 1 || choice >(int)configs.size()) {
                return 1;
            }
            config_name = configs[choice - 1];
        }
    
        std::cout << ">> Running daemon for config: " << config_name << "\n";

        std::filesystem::path data_dir = getDataDirForConfig(config_name);
#if CUSTOM_LOGGING_ENABLED
        try {
            std::filesystem::create_directories(data_dir / "logs");
            Logger::get().addLogFile("general", (data_dir / "logs" / "syncharbor-daemon.log").string());
            
        }
        catch (const std::exception& e) {
            std::cerr << "Cannot create data folder: " << (data_dir / "logs") << ": " << e.what() << "\n";
            return 1;
        }
#endif

        std::string db_full_path = (data_dir / (config_name + ".sqlite3")).string();

        sync_manager = std::make_unique<SyncManager>(db_full_path, SyncManager::Mode::Daemon);

#ifdef NDEBUG
        if (!backgroundize()) {
            std::cerr << "Warning: backgroundize failed; running in foreground\n";
        }

        {
            std::filesystem::path pid_file = data_dir / (config_name + ".pid");
            std::ofstream out(pid_file, std::ios::trunc);
            if (!out) {
                std::cerr << "Cannot write pid file: " << pid_file << "\n";
            }
            else {
                process_id_t mypid;
#ifdef _WIN32
                mypid = GetCurrentProcessId();
#else
                mypid = getpid();
#endif
                out << mypid << "\n";
                out.close();
            }
        }
#else
        std::cout << "[DEBUG] Running in foreground (daemonize skipped)\n";
#endif

#ifdef _WIN32
        SetConsoleCtrlHandler(ConsoleHandler, TRUE);
#else
        std::signal(SIGINT, on_sigint);
        std::signal(SIGTERM, on_sigint);
#endif

        sync_manager->run();
    }
#ifdef WITH_DEV_MODES
    else if (mode == "--config-file-initial") {
        if (config_name.empty()) {
            std::cerr << "Usage: " << argv[0] << " --config-file-initial <cfg.json>\n";
            return 1;
        }

        std::filesystem::path data_dir = getDataDirForConfig(config_name);
        std::filesystem::create_directories(data_dir);

        try {
            std::filesystem::create_directories(data_dir / "logs");
            Logger::get().addLogFile("general", (data_dir / "logs" / "syncharbor-daemon.log").string());

        }
        catch (const std::exception& e) {
            std::cerr << "Cannot create data folder: " << (data_dir / "logs") << ": " << e.what() << "\n";
            return 1;
        }

        std::string db_full_path = (data_dir / (config_name + ".sqlite3")).string();

        sync_manager = std::make_unique<SyncManager>(config_name, db_full_path, SyncManager::Mode::InitialSync);
        sync_manager->run();
        sync_manager->shutdown();
        return 0;
    }
    else if (mode == "--config-file-daemon") {
        if (config_name.empty()) {
            std::cerr << "Usage: " << argv[0] << " --config-file-daemon <cfg.json>\n";
            return 1;
        }

        std::filesystem::path data_dir = getDataDirForConfig(config_name);

        try {
            std::filesystem::create_directories(data_dir / "logs");
            Logger::get().addLogFile("general", (data_dir / "logs" / "syncharbor-daemon.log").string());

        }
        catch (const std::exception& e) {
            std::cerr << "Cannot create data folder: " << (data_dir / "logs") << ": " << e.what() << "\n";
            return 1;
        }

        std::string db_full_path = (data_dir / (config_name + ".sqlite3")).string();

        sync_manager = std::make_unique<SyncManager>(config_name, db_full_path, SyncManager::Mode::Daemon);

#ifdef NDEBUG
        if (!backgroundize()) {
            std::cerr << "Warning: backgroundize failed; running in foreground\n";
        }

        {
            std::filesystem::path pid_file = data_dir / (config_name + ".pid");
            std::ofstream out(pid_file, std::ios::trunc);
            if (!out) {
                std::cerr << "Cannot write pid file: " << pid_file << "\n";
            }
            else {
                process_id_t mypid;
#ifdef _WIN32
                mypid = GetCurrentProcessId();
#else
                mypid = getpid();
#endif
                out << mypid << "\n";
                out.close();
            }
        }

#else
        std::cout << "[DEBUG] Running in foreground (daemonize skipped)\n";
#endif


#ifdef _WIN32
    SetConsoleCtrlHandler(ConsoleHandler, TRUE);
#else
    std::signal(SIGINT, on_sigint);
    std::signal(SIGTERM, on_sigint);
#endif

    sync_manager->run();
        
    }
#else
    else if (mode == "--config-file-initial" || mode == "--config-file-daemon") {
        std::cerr << "Mode '" << mode << "' is only available in Debug/RelWithDebInfo builds.\n";
        return 1;
    }
#endif
    else {
        std::cerr << "Unknown mode: " << mode << "\n";
        return 1;
    }

    LOG_DEBUG("Main", "Waiting for stop signal...");
    while (!g_should_exit.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }
    LOG_DEBUG("Main", "Recieved SIGINT/SIGTERM. Shutting down...");
    if (sync_manager) {
        sync_manager->shutdown();
    }
    return 0;
}
