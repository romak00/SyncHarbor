#include "LocalStorage.h"

LocalStorage::LocalStorage(const std::filesystem::path& home_dir, const int cloud_id, const std::string& db_file_name) {
    _local_home_dir = home_dir;
    _id = cloud_id;
    _db = std::make_unique<Database>(db_file_name);

    _listener = std::make_unique<Listener>(this);
}

LocalStorage::~LocalStorage() {
    stopWatching();
}

void LocalStorage::startWatching() {
    if (_watching) return;

    _watching = true;
    _watcher_thread = std::make_unique<std::thread>([this] {
        _watch_id = _watcher.addWatch(_local_home_dir.string(), _listener.get(), true);
        _watcher.watch();
        });
    _changes_filtering_worker = std::make_unique<std::thread>(&LocalStorage::changesFilter, this);
}

void LocalStorage::stopWatching() {
    if (!_watching) return;

    _watching = false;
    _watcher.removeWatch(_watch_id);
    _events_queue.notify();
    if (_watcher_thread && _watcher_thread->joinable()) {
        _watcher_thread->join();
    }
    _changes_queue.notify();
    if (_changes_filtering_worker && _changes_filtering_worker->joinable()) {
        _changes_filtering_worker->join();
    }
}

void LocalStorage::onFileChanged(
    const std::string& dir,
    const std::string& filename,
    efsw::Action action,
    const std::string& oldFilename
)
{
    FileEvent evt;
    evt.new_path = std::filesystem::path(dir) / filename;

    switch (action) {
    case efsw::Actions::Add:
        evt.type = FileEventType::Added;
        break;
    case efsw::Actions::Delete:
        evt.type = FileEventType::Deleted;
        break;
    case efsw::Actions::Modified:
        evt.type = FileEventType::Modified;
        break;
    case efsw::Actions::Moved:
        evt.type = FileEventType::Moved;
        evt.old_path = std::filesystem::path(dir) / oldFilename;
        break;
    default:
        return;
    }

    _events_queue.push(std::move(evt));

}

void LocalStorage::changesFilter() {
    FileEvent evt;
    while (_watching || !_changes_queue.empty() || !_events_queue.empty()) {
        if (_events_queue.pop(evt, [&] { return !_watching; })) {
            std::unique_ptr<ChangeDTO> dto = std::make_unique<ChangeDTO>();
            switch (evt.type) {
            case FileEventType::Added:
                break;
            case FileEventType::Deleted:
                break;
            case FileEventType::Modified:
                break;
            case FileEventType::Moved:
                break;
            }
            if (dto) {
                _changes_queue.push(std::move(dto));
            }
        }
    }
}