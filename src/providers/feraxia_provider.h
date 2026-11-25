#pragma once

#include "cloud_provider.h"
#include "cloud_provider_base.h"

#include <memory>
#include <string>

class FeraxiaProvider : public ICloudProvider {
public:
    const char* Name() const override { return "Feraxia"; }
    bool Init(const std::string& configPath) override;
    void Shutdown() override;
    bool IsAuthenticated() const override;
    bool Upload(const std::string& path, const uint8_t* data, size_t len) override;
    bool Download(const std::string& path, std::vector<uint8_t>& outData) override;
    bool Remove(const std::string& path) override;
    ExistsStatus CheckExists(const std::string& path) override;
    std::vector<FileInfo> List(const std::string& prefix) override;
    bool ListChecked(const std::string& prefix, std::vector<FileInfo>& outFiles,
                     bool* outComplete = nullptr) override;

private:
    bool ParsePath(const std::string& path, std::string& appId, std::string& relativePath) const;
    HttpUtil::HttpResp Request(const char* method, const std::string& path,
                               const std::string& body = {},
                               const std::vector<std::string>& headers = {});

    std::string m_apiUrl;
    std::string m_credential;
    std::unique_ptr<IHttpTransport> m_transport;
};
