#include "feraxia_provider.h"

#include "http_util.h"
#include "json.h"
#include "log.h"

#include <cctype>

namespace {
bool ValidAppId(const std::string& value) {
    if (value.empty() || value.size() > 10 || value[0] == '0') return false;
    for (char c : value) if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

bool ValidRelativePath(const std::string& value) {
    if (value.empty() || value.front() == '/' || value.find('\\') != std::string::npos) return false;
    size_t start = 0;
    while (start < value.size()) {
        size_t end = value.find('/', start);
        std::string segment = value.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (segment.empty() || segment == "." || segment == "..") return false;
        start = end == std::string::npos ? value.size() : end + 1;
    }
    return true;
}
}

bool FeraxiaProvider::Init(const std::string& configPath) {
    auto store = CreateTokenStore();
    if (!store) return false;
    const auto root = Json::Parse(store->Read(configPath));
    if (root.type != Json::Type::Object) return false;
    m_apiUrl = root["api_url"].str();
    m_credential = root["credential"].str();
    while (!m_apiUrl.empty() && m_apiUrl.back() == '/') m_apiUrl.pop_back();
    if (m_apiUrl.rfind("https://", 0) != 0 || m_credential.empty()) return false;
    m_transport = CreateHttpTransport("[Feraxia]");
    return m_transport && m_transport->Init();
}

void FeraxiaProvider::Shutdown() {
    if (m_transport) m_transport->Shutdown();
    m_transport.reset();
    m_credential.clear();
    m_apiUrl.clear();
}

bool FeraxiaProvider::IsAuthenticated() const {
    return m_transport && m_transport->IsReady() && !m_credential.empty();
}

bool FeraxiaProvider::ParsePath(const std::string& path, std::string& appId, std::string& relativePath) const {
    const size_t first = path.find('/');
    const size_t second = first == std::string::npos ? std::string::npos : path.find('/', first + 1);
    if (second == std::string::npos) return false;
    appId = path.substr(first + 1, second - first - 1);
    relativePath = path.substr(second + 1);
    return ValidAppId(appId) && ValidRelativePath(relativePath);
}

HttpUtil::HttpResp FeraxiaProvider::Request(const char* method, const std::string& path,
                                            const std::string& body,
                                            const std::vector<std::string>& headers) {
    if (!IsAuthenticated()) return {};
    std::vector<std::string> requestHeaders = headers;
    requestHeaders.push_back("Authorization: Bearer " + m_credential);
    return m_transport->RequestUrl(method, m_apiUrl + path, body, requestHeaders);
}

bool FeraxiaProvider::Upload(const std::string& path, const uint8_t* data, size_t len) {
    std::string appId, relative;
    if (!ParsePath(path, appId, relative)) return false;
    std::string body(reinterpret_cast<const char*>(data), len);
    const auto response = Request("PUT", "/cloud/v1/apps/" + appId + "/objects/" + HttpUtil::UrlEncode(relative, true), body,
                                  {"Content-Type: application/octet-stream", "Content-Length: " + std::to_string(len)});
    return response.status == 201 || response.status == 200;
}

bool FeraxiaProvider::Download(const std::string& path, std::vector<uint8_t>& outData) {
    std::string appId, relative;
    if (!ParsePath(path, appId, relative)) return false;
    const auto response = Request("GET", "/cloud/v1/apps/" + appId + "/objects/" + HttpUtil::UrlEncode(relative, true));
    if (response.status != 200) return false;
    outData.assign(response.body.begin(), response.body.end());
    return true;
}

bool FeraxiaProvider::Remove(const std::string& path) {
    std::string appId, relative;
    if (!ParsePath(path, appId, relative)) return false;
    const auto response = Request("DELETE", "/cloud/v1/apps/" + appId + "/objects/" + HttpUtil::UrlEncode(relative, true));
    return response.status == 204 || response.status == 404;
}

ICloudProvider::ExistsStatus FeraxiaProvider::CheckExists(const std::string& path) {
    std::string appId, relative;
    if (!ParsePath(path, appId, relative)) return ExistsStatus::Error;
    const auto response = Request("HEAD", "/cloud/v1/apps/" + appId + "/objects/" + HttpUtil::UrlEncode(relative, true));
    if (response.status == 200) return ExistsStatus::Exists;
    if (response.status == 404) return ExistsStatus::Missing;
    return ExistsStatus::Error;
}

std::vector<ICloudProvider::FileInfo> FeraxiaProvider::List(const std::string& prefix) {
    std::vector<FileInfo> files;
    ListChecked(prefix, files, nullptr);
    return files;
}

bool FeraxiaProvider::ListChecked(const std::string& prefix, std::vector<FileInfo>& outFiles, bool* outComplete) {
    outFiles.clear();
    if (outComplete) *outComplete = false;
    std::string appId, relative;
    if (!ParsePath(prefix + "_", appId, relative)) return false;
    std::string cursor;
    do {
        std::string query = "?prefix=" + HttpUtil::UrlEncode(prefix.substr(prefix.find('/', prefix.find('/') + 1) + 1), true);
        if (!cursor.empty()) query += "&cursor=" + HttpUtil::UrlEncode(cursor, false);
        const auto response = Request("GET", "/cloud/v1/apps/" + appId + "/objects" + query);
        if (response.status != 200) return false;
        const auto root = Json::Parse(response.body);
        for (const auto& item : root["objects"].arrVal) {
            if (item.type != Json::Type::Object) continue;
            FileInfo file;
            file.path = prefix.substr(0, prefix.find('/', prefix.find('/') + 1) + 1) + item["path"].str();
            file.size = static_cast<uint64_t>(item["size"].number());
            outFiles.push_back(std::move(file));
        }
        cursor = root["nextCursor"].str();
    } while (!cursor.empty());
    if (outComplete) *outComplete = true;
    return true;
}
