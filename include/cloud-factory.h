#pragma once

#include "BaseStorage.h"

enum class CloudProviderType;
class GoogleDrive;
class Dropbox;

class CloudFactory {
public:
    template <typename... Args>
    static auto create(const CloudProviderType type, Args&&... args) -> std::shared_ptr<BaseStorage>
    {
        switch (type) {
        case CloudProviderType::Dropbox:
            return std::make_shared<Dropbox>(std::forward<Args>(args)...);
        case CloudProviderType::GoogleDrive:
            return std::make_shared<GoogleDrive>(std::forward<Args>(args)...);
        default:
            throw std::invalid_argument("CloudFactory create(): Unknown CloudProviderType");
        }
    }
};