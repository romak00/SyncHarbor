#pragma once

#include <string>
#include <unordered_map>

enum class CloudProviderType;

#ifndef GOOGLE_DEFAULT_CLIENT_ID
#define GOOGLE_DEFAULT_CLIENT_ID "425163723279-2mqd9578g7tkd7gpq2butd4sjae59djg.apps.googleusercontent.com"
#endif
#ifndef GOOGLE_DEFAULT_CLIENT_SECRET
#define GOOGLE_DEFAULT_CLIENT_SECRET "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"
#endif
#ifndef DROPBOX_DEFAULT_CLIENT_ID
#define DROPBOX_DEFAULT_CLIENT_ID "qc1cw5yaa3dac1d"
#endif
#ifndef DROPBOX_DEFAULT_CLIENT_SECRET
#define DROPBOX_DEFAULT_CLIENT_SECRET "d0bcxwa1ejd56u0"
#endif

struct CloudFactoryInfo {
    CloudProviderType type;
    std::string default_client_id;
    std::string default_client_secret;
};

static const std::unordered_map<std::string, CloudFactoryInfo> CloudRegistry = {
    { "GoogleDrive", { CloudProviderType::GoogleDrive, GOOGLE_DEFAULT_CLIENT_ID, GOOGLE_DEFAULT_CLIENT_SECRET } },
    { "Dropbox",     { CloudProviderType::Dropbox, DROPBOX_DEFAULT_CLIENT_ID, DROPBOX_DEFAULT_CLIENT_SECRET } },
};