#include "utils.h"

std::filesystem::path normalizePath(const std::filesystem::path& p) {
    std::string s = p.generic_string();

    if (s == "/") {
        return std::filesystem::path{};
    }

    if (!s.empty() && s.front() == '/') {
        s.erase(0, 1);
    }

    return std::filesystem::path{ s };
}

const char* to_cstr(CloudProviderType type) {
    switch (type)
    {
    case CloudProviderType::LocalStorage: return "LocalStorage";
    case CloudProviderType::GoogleDrive: return "GoogleDrive";
    case CloudProviderType::Dropbox: return "Dropbox";
    case CloudProviderType::OneDrive: return "OneDrive";
    case CloudProviderType::Yandex: return "Yandex";
    case CloudProviderType::MailRu: return "MailRu";
    case CloudProviderType::FakeTest: return "FakeTest";
    default: return "Unknown";
    }
}

std::string_view to_string(CloudProviderType type) {
    return to_cstr(type);
}

CloudProviderType cloud_type_from_string(std::string_view str) {
    static const std::unordered_map<std::string_view, CloudProviderType> map =
    {
        {"LocalStorage", CloudProviderType::LocalStorage},
        {"GoogleDrive", CloudProviderType::GoogleDrive},
        {"Dropbox", CloudProviderType::Dropbox},
        {"OneDrive", CloudProviderType::OneDrive},
        {"Yandex", CloudProviderType::Yandex},
        {"MailRu", CloudProviderType::MailRu},
        {"FakeTest", CloudProviderType::FakeTest}
    };

    return map.at(str);
}

std::time_t convertCloudTime(std::string datetime) {
    if (!datetime.empty() && datetime.back() == 'Z')
        datetime.pop_back();
    auto dotPos = datetime.find('.');
    if (dotPos != std::string::npos)
        datetime.resize(dotPos);

    std::tm tm = {};
    std::istringstream iss(datetime);
    iss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    tm.tm_isdst = 0;

#if defined(_WIN32) || defined(_WIN64)
    return _mkgmtime(&tm);
#else
    return timegm(&tm);
#endif
}

std::time_t convertSystemTime(const std::filesystem::path& p) {
    using namespace std::chrono;
    auto ftime = std::filesystem::last_write_time(p);

#if defined(__cpp_lib_chrono) && __cpp_lib_chrono >= 201907L && !defined(_MSC_VER)
    auto sctp = std::filesystem::file_time_type::clock::to_sys(ftime);
    return system_clock::to_time_t(sctp);
#else
    using file_clock = decltype(ftime)::clock;
    auto now_file = file_clock::now();
    auto diff = ftime - now_file;
    auto diff_sys = duration_cast<system_clock::duration>(diff);
    auto sctp = system_clock::now() + diff_sys;
    return system_clock::to_time_t(sctp);
#endif
}

const char* to_cstr(EntryType type) {
    switch (type)
    {
    case EntryType::File: return "File";
    case EntryType::Directory: return "Directory";
    case EntryType::Document: return "Document";
    default: return "Null";
    }
}

std::string_view to_string(EntryType type) {
    return to_cstr(type);
}

EntryType entry_type_from_string(std::string_view str) {
    static const std::unordered_map<std::string_view, EntryType> map =
    {
        {"File", EntryType::File},
        {"Directory", EntryType::Directory},
        {"Document", EntryType::Document},
        {"Null", EntryType::Null}
    };

    auto it = map.find(str);
    if (it != map.end()) {
        return it->second;
    }
    return EntryType::Null;
}

std::string to_string(ChangeType ch) {
    switch (ch)
    {
    case ChangeType::New: return "New";
    case ChangeType::Rename: return "Rename";
    case ChangeType::Update: return "Update";
    case ChangeType::Move: return "Move";
    case ChangeType::Delete: return "Delete";
    default: return "Null";
    }
}