#include "commands.h"
#include "logger.h"
#include "change.h"

void ICommand::setDTO(std::unique_ptr<FileRecordDTO> dto) {}
void ICommand::setDTO(std::unique_ptr<FileUpdatedDTO> dto) {}
void ICommand::setDTO(std::unique_ptr<FileDeletedDTO> dto) {}
void ICommand::setDTO(std::unique_ptr<FileMovedDTO> dto) {}

RequestHandle& ICommand::getHandle() {
    static RequestHandle dummy;
    return dummy;
}

EntryType ICommand::getTargetType() const {
    return EntryType::Null;
}

bool ICommand::needRepeat() const {
    return false;
}

void ICommand::setOwner(std::weak_ptr<Change> ow) noexcept {
    _owner = std::move(ow);
    if (auto ch = owner()) {
        ch->onCommandCreated();
    }
}

std::shared_ptr<Change> ICommand::owner() const noexcept {
    return _owner.lock();
}

void ChainedCommand::addNext(std::unique_ptr<ICommand> next_command) {
    _next_commands.emplace_back(std::move(next_command));
}

int ChainedCommand::getId() const {
    return _cloud_id;
}

void CloudCommand::continueChain() {
    for (auto& next : _next_commands) {
        CallbackDispatcher::get().submit(std::move(next));
    }
}

void LocalCommand::continueChain() {
    for (auto& next : _next_commands) {
        HttpClient::get().submit(std::move(next));
    }
}

LocalUploadCommand::LocalUploadCommand(const int cloud_id) {
    _cloud_id = cloud_id;
}

void LocalUploadCommand::completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) {

    LOG_INFO("LOCAL UPLOAD %s started", this->getTarget());
    cloud->proccesUpload(_dto, "");
    for (auto& next_command : _next_commands) {
        int cloud_id = next_command->getId();
        _dto->cloud_parent_id = db->getCloudFileIdByPath(_dto->rel_path.parent_path(), cloud_id);
        _dto->cloud_id = cloud_id;
        next_command->setDTO(std::make_unique<FileRecordDTO>(*_dto));
    }
    LOG_INFO("LOCAL UPLOAD %s completed", this->getTarget());

    if (auto ch = owner()) {
        ch->onCommandFinished();
    }
    continueChain();
}
void LocalUploadCommand::setDTO(std::unique_ptr<FileRecordDTO> dto) {
    _dto = std::move(dto);
}

std::string LocalUploadCommand::getTarget() const {
    return _dto->rel_path.string();
}

EntryType LocalUploadCommand::getTargetType() const {
    return _dto->type;
}

LocalUpdateCommand::LocalUpdateCommand(const int cloud_id) {
    _cloud_id = cloud_id;
}

void LocalUpdateCommand::completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) {
    LOG_INFO("LOCAL UPDATE", this->getTarget(), "started");

    cloud->proccesUpdate(_dto, "");
    db->update_file(*_dto);

    for (auto& next_command : _next_commands) {
        int cloud_id = next_command->getId();
        int global_id = _dto->global_id;
        std::string cloud_file_id = db->get_cloud_file_id_by_cloud_id(cloud_id, global_id);
        _dto->cloud_file_id = cloud_file_id;
        _dto->cloud_id = cloud_id;
        next_command->setDTO(std::make_unique<FileUpdatedDTO>(*_dto));
    }
    LOG_INFO("LOCAL UPDATE", this->getTarget(), "completed");

    if (auto ch = owner()) {
        ch->onCommandFinished();
    }
    continueChain();
}
void LocalUpdateCommand::setDTO(std::unique_ptr<FileUpdatedDTO> dto) {
    _dto = std::move(dto);
}

std::string LocalUpdateCommand::getTarget() const {
    return _dto->rel_path.string();
}

EntryType LocalUpdateCommand::getTargetType() const {
    return _dto->type;
}

LocalMoveCommand::LocalMoveCommand(const int cloud_id) {
    _cloud_id = cloud_id;
}

void LocalMoveCommand::completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) {
    LOG_INFO("LOCAL MOVE %s started", this->getTarget());

    cloud->proccesMove(_dto, "");

    for (auto& next_command : _next_commands) {
        int cloud_id = next_command->getId();
        int global_id = _dto->global_id;
        auto cloud_dto = db->getFileByCloudIdAndGlobalId(cloud_id, global_id);
        _dto->cloud_file_id = cloud_dto->cloud_file_id;
        _dto->old_cloud_parent_id = cloud_dto->cloud_parent_id;
        _dto->new_cloud_parent_id = db->getCloudFileIdByPath(_dto->new_rel_path.parent_path(), cloud_id);
        _dto->cloud_id = cloud_id;
        next_command->setDTO(std::make_unique<FileMovedDTO>(*_dto));
    }
    LOG_INFO("LOCAL MOVE %s completed", this->getTarget());

    if (auto ch = owner()) {
        ch->onCommandFinished();
    }
    continueChain();
}
void LocalMoveCommand::setDTO(std::unique_ptr<FileMovedDTO> dto) {
    _dto = std::move(dto);
}

std::string LocalMoveCommand::getTarget() const {
    return _dto->old_rel_path.string();
}

EntryType LocalMoveCommand::getTargetType() const {
    return _dto->type;
}

LocalDeleteCommand::LocalDeleteCommand(const int cloud_id) {
    _cloud_id = cloud_id;
}

void LocalDeleteCommand::completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) {
    bool need_local_delete = (_dto->cloud_id != 0);

    LOG_INFO("LOCAL MOVE", this->getTarget(), "started");

    cloud->proccesDelete(_dto, "");

    for (auto& next_command : _next_commands) {
        int cloud_id = next_command->getId();
        int global_id = _dto->global_id;
        std::string cloud_file_id = db->get_cloud_file_id_by_cloud_id(cloud_id, global_id);
        _dto->cloud_file_id = cloud_file_id;
        _dto->cloud_id = cloud_id;
        next_command->setDTO(std::make_unique<FileDeletedDTO>(*_dto));
    }
    if (need_local_delete) {
        std::string path = cloud->getHomeDir() + "/" + _dto->rel_path.string();
        std::filesystem::remove(path);
    }
    db->delete_file_and_links(_dto->global_id);
    LOG_INFO("LOCAL DELETE", this->getTarget(), "completed");

    if (auto ch = owner()) {
        ch->onCommandFinished();
    }
    continueChain();
}

void LocalDeleteCommand::setDTO(std::unique_ptr<FileDeletedDTO> dto) {
    _dto = std::move(dto);
}

std::string LocalDeleteCommand::getTarget() const {
    return _dto->rel_path.string();
}

CloudUploadCommand::CloudUploadCommand(const int cloud_id) {
    _cloud_id = cloud_id;
}

void CloudUploadCommand::execute(const std::shared_ptr<BaseStorage>& cloud) {
    _dto->cloud_id = _cloud_id;
    _handle = std::make_unique<RequestHandle>();
    cloud->setupUploadHandle(_handle, _dto);

    LOG_INFO("CLOUD UPLOAD", "Started for entry: %s on: %s", this->getTarget(), CloudResolver::getName(_cloud_id));
}

void CloudUploadCommand::completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) {
    cloud->proccesUpload(_dto, _handle->_response);
    db->add_file_link(*_dto);
    LOG_INFO("CLOUD UPLOAD", "Completed for entry: %s on: %s", this->getTarget(), CloudResolver::getName(_cloud_id));
    for (auto& next_command : _next_commands) {
        next_command->setDTO(std::make_unique<FileRecordDTO>(*_dto));
    }

    if (auto ch = owner()) {
        ch->onCommandFinished();
    }
    continueChain();
}

void CloudUploadCommand::setDTO(std::unique_ptr<FileRecordDTO> dto) {
    _dto = std::move(dto);
}

RequestHandle& CloudUploadCommand::getHandle() {
    return *_handle;
}

std::string CloudUploadCommand::getTarget() const {
    return _dto->rel_path.string();
}

EntryType CloudUploadCommand::getTargetType() const {
    return _dto->type;
}

CloudUpdateCommand::CloudUpdateCommand(const int cloud_id) {
    _cloud_id = cloud_id;
}

void CloudUpdateCommand::execute(const std::shared_ptr<BaseStorage>& cloud) {
    _dto->cloud_id = _cloud_id;
    _handle = std::make_unique<RequestHandle>();
    cloud->setupUpdateHandle(_handle, _dto);

    LOG_INFO("CLOUD UPDATE", "Started for entry: %s on: %s", this->getTarget(), CloudResolver::getName(_cloud_id));
}

void CloudUpdateCommand::completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) {
    cloud->proccesUpdate(_dto, _handle->_response);
    db->update_file_link(*_dto);
    LOG_INFO("CLOUD UPDATE", "Completed for entry: %s on: %s", this->getTarget(), CloudResolver::getName(_cloud_id));
    for (auto& next_command : _next_commands) {
        next_command->setDTO(std::make_unique<FileUpdatedDTO>(*_dto));
    }

    if (auto ch = owner()) {
        ch->onCommandFinished();
    }
    continueChain();
}

RequestHandle& CloudUpdateCommand::getHandle() {
    return *_handle;
}

void CloudUpdateCommand::setDTO(std::unique_ptr<FileUpdatedDTO> dto) {
    _dto = std::move(dto);
}

std::string CloudUpdateCommand::getTarget() const {
    return _dto->rel_path.filename().string();
}

EntryType CloudUpdateCommand::getTargetType() const {
    return _dto->type;
}

CloudMoveCommand::CloudMoveCommand(const int cloud_id) {
    _cloud_id = cloud_id;
}

void CloudMoveCommand::execute(const std::shared_ptr<BaseStorage>& cloud) {
    _dto->cloud_id = _cloud_id;
    _handle = std::make_unique<RequestHandle>();
    cloud->setupMoveHandle(_handle, _dto);

    LOG_INFO("CLOUD MOVE", "Started for entry: %s on: %s", this->getTarget(), CloudResolver::getName(_cloud_id));
}

void CloudMoveCommand::completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) {
    cloud->proccesMove(_dto, _handle->_response);
    db->update_file_link(*_dto);
    LOG_INFO("CLOUD MOVE", "Completed for entry: %s on: %s", this->getTarget(), CloudResolver::getName(_cloud_id));
    for (auto& next_command : _next_commands) {
        next_command->setDTO(std::make_unique<FileMovedDTO>(*_dto));
    }

    if (auto ch = owner()) {
        ch->onCommandFinished();
    }
    continueChain();
}

RequestHandle& CloudMoveCommand::getHandle() {
    return *_handle;
}

void CloudMoveCommand::setDTO(std::unique_ptr<FileMovedDTO> dto) {
    _dto = std::move(dto);
}

std::string CloudMoveCommand::getTarget() const {
    return _dto->old_rel_path.filename().string();
}

EntryType CloudMoveCommand::getTargetType() const {
    return _dto->type;
}

CloudDownloadNewCommand::CloudDownloadNewCommand(const int cloud_id) {
    _cloud_id = cloud_id;
}

void CloudDownloadNewCommand::execute(const std::shared_ptr<BaseStorage>& cloud) {
    LOG_INFO("CLOUD DOWNLOAD", "New file download started: %s", _dto->rel_path.string().c_str());
    _dto->cloud_id = _cloud_id;
    _handle = std::make_unique<RequestHandle>();
    cloud->setupDownloadHandle(_handle, _dto);
}

void CloudDownloadNewCommand::completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) {
    LOG_INFO("CLOUD DOWNLOAD", "New file downloaded: %s", _dto->rel_path.string().c_str());
    for (auto& next_command : _next_commands) {
        next_command->setDTO(std::make_unique<FileRecordDTO>(*_dto));
    }

    if (auto ch = owner()) {
        ch->onCommandFinished();
    }
    continueChain();
}

RequestHandle& CloudDownloadNewCommand::getHandle() {
    return *_handle;
}

void CloudDownloadNewCommand::setDTO(std::unique_ptr<FileRecordDTO> dto) {
    _dto = std::move(dto);
}

std::string CloudDownloadNewCommand::getTarget() const {
    return _dto->rel_path.filename().string();
}

EntryType CloudDownloadNewCommand::getTargetType() const {
    return _dto->type;
}

CloudDownloadUpdateCommand::CloudDownloadUpdateCommand(const int cloud_id) {
    _cloud_id = cloud_id;
}

void CloudDownloadUpdateCommand::execute(const std::shared_ptr<BaseStorage>& cloud) {
    LOG_INFO("CLOUD DOWNLOAD", "Update file download started: %s", _dto->rel_path.string().c_str());
    _dto->cloud_id = _cloud_id;
    _handle = std::make_unique<RequestHandle>();
    cloud->setupDownloadHandle(_handle, _dto);
}

void CloudDownloadUpdateCommand::completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) {
    LOG_INFO("CLOUD DOWNLOAD", "Updated file downloaded (tmp): %s",
        _dto->rel_path.string().c_str());
    for (auto& next_command : _next_commands) {
        next_command->setDTO(std::make_unique<FileUpdatedDTO>(*_dto));
    }

    if (auto ch = owner()) {
        ch->onCommandFinished();
    }
    continueChain();
}

RequestHandle& CloudDownloadUpdateCommand::getHandle() {
    return *_handle;
}

void CloudDownloadUpdateCommand::setDTO(std::unique_ptr<FileUpdatedDTO> dto) {
    _dto = std::move(dto);
}

std::string CloudDownloadUpdateCommand::getTarget() const {
    return _dto->rel_path.filename().string();
}

EntryType CloudDownloadUpdateCommand::getTargetType() const {
    return _dto->type;
}

CloudDeleteCommand::CloudDeleteCommand(const int cloud_id) {
    _cloud_id = cloud_id;
}

void CloudDeleteCommand::execute(const std::shared_ptr<BaseStorage>& cloud) {
    _dto->cloud_id = _cloud_id;
    _handle = std::make_unique<RequestHandle>();
    cloud->setupDeleteHandle(_handle, _dto);

    LOG_INFO("CLOUD DELETE", "Started for entry: %s on: %s", this->getTarget(), CloudResolver::getName(_cloud_id));
}

void CloudDeleteCommand::completionCallback(const std::unique_ptr<Database>& db, const std::shared_ptr<BaseStorage>& cloud) {
    cloud->proccesDelete(_dto, _handle->_response);
    LOG_INFO("CLOUD DELETE", "Completed for entry: %s on: %s", this->getTarget(), CloudResolver::getName(_cloud_id));
    for (auto& next_command : _next_commands) {
        next_command->setDTO(std::make_unique<FileDeletedDTO>(*_dto));
    }

    if (auto ch = owner()) {
        ch->onCommandFinished();
    }
    continueChain();
}

void CloudDeleteCommand::setDTO(std::unique_ptr<FileDeletedDTO> dto) {
    _dto = std::move(dto);
}

RequestHandle& CloudDeleteCommand::getHandle() {
    return *_handle;
}

std::string CloudDeleteCommand::getTarget() const {
    return _dto->rel_path.string();
}
