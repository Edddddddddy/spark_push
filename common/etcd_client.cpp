#include "etcd_client.h"

#include "logging.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <sstream>
#include <thread>
#include <vector>

namespace sparkpush {

namespace {

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  const size_t bytes = size * nmemb;
  auto* out = static_cast<std::string*>(userp);
  out->append(static_cast<char*>(contents), bytes);
  return bytes;
}

std::string TrimSlashes(std::string value) {
  while (!value.empty() && value.back() == '/') value.pop_back();
  return value;
}

std::string NormalizePrefix(std::string prefix) {
  if (prefix.empty()) return "/sparkpush/services";
  if (prefix.front() != '/') prefix.insert(prefix.begin(), '/');
  while (prefix.size() > 1 && prefix.back() == '/') prefix.pop_back();
  return prefix;
}

std::vector<std::string> SplitCsv(const std::string& csv) {
  std::vector<std::string> values;
  std::stringstream ss(csv);
  std::string item;
  while (std::getline(ss, item, ',')) {
    if (!item.empty()) values.push_back(item);
  }
  return values;
}

void CollectLeafNodes(const nlohmann::json& node,
                      std::unordered_map<std::string, std::string>* instances) {
  if (!node.is_object()) return;

  if (node.value("dir", false)) {
    if (node.contains("nodes") && node["nodes"].is_array()) {
      for (const auto& child : node["nodes"]) {
        CollectLeafNodes(child, instances);
      }
    }
    return;
  }

  const std::string key = node.value("key", "");
  const std::string value = node.value("value", "");
  if (key.empty() || value.empty()) return;

  const size_t pos = key.find_last_of('/');
  const std::string instance = (pos == std::string::npos) ? key : key.substr(pos + 1);
  if (!instance.empty()) {
    (*instances)[instance] = value;
  }
}

}  // namespace

EtcdClient::EtcdClient(std::string endpoints, std::string prefix)
    : prefix_(NormalizePrefix(std::move(prefix))) {
  const auto values = SplitCsv(endpoints);
  if (!values.empty()) {
    endpoint_ = TrimSlashes(values.front());
  }
}

bool EtcdClient::Enabled() const {
  return !endpoint_.empty();
}

bool EtcdClient::Request(const std::string& method,
                         const std::string& path,
                         const std::string& body,
                         std::string* response_body,
                         long* status_code) {
  if (!Enabled()) return false;
  static const bool kCurlInitialized = []() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    return true;
  }();
  (void)kCurlInitialized;

  CURL* curl = curl_easy_init();
  if (!curl) return false;

  std::string response;
  const std::string url = endpoint_ + path;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 3000L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

  struct curl_slist* headers = nullptr;
  headers =
      curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  if (method == "PUT") {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  } else if (method == "DELETE") {
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
  } else {
    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
  }

  const CURLcode rc = curl_easy_perform(curl);
  long code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
  if (response_body) *response_body = response;
  if (status_code) *status_code = code;

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (rc != CURLE_OK) {
    LogError("[etcd] request failed: " + url + " error=" + curl_easy_strerror(rc));
    return false;
  }
  return true;
}

std::string EtcdClient::BuildServicePath(const std::string& service,
                                         const std::string& instance) const {
  std::string path = "/v2/keys" + prefix_ + "/" + service;
  if (!instance.empty()) path += "/" + instance;
  return path;
}

bool EtcdClient::PutService(const std::string& service,
                            const std::string& instance,
                            const std::string& value,
                            int ttl_seconds) {
  if (!Enabled()) return false;
  static const bool kCurlInitialized = []() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
    return true;
  }();
  (void)kCurlInitialized;

  CURL* curl = curl_easy_init();
  if (!curl) return false;
  char* escaped = curl_easy_escape(curl, value.c_str(), static_cast<int>(value.size()));
  std::string body = "value=" + std::string(escaped ? escaped : "");
  if (ttl_seconds > 0) {
    body += "&ttl=" + std::to_string(ttl_seconds);
  }
  curl_free(escaped);
  curl_easy_cleanup(curl);

  long status = 0;
  std::string response;
  const bool ok = Request("PUT", BuildServicePath(service, instance), body, &response, &status);
  return ok && status >= 200 && status < 300;
}

bool EtcdClient::DeleteService(const std::string& service,
                               const std::string& instance) {
  long status = 0;
  std::string response;
  const bool ok =
      Request("DELETE", BuildServicePath(service, instance), "", &response, &status);
  return ok && (status == 200 || status == 404);
}

bool EtcdClient::ListServiceAddresses(
    const std::string& service,
    std::unordered_map<std::string, std::string>* instances) {
  if (!instances) return false;
  instances->clear();

  long status = 0;
  std::string response;
  const bool ok = Request("GET",
                          BuildServicePath(service) + "?recursive=true",
                          "",
                          &response,
                          &status);
  if (!ok || status == 404) return false;
  if (status < 200 || status >= 300) return false;

  try {
    const auto j = nlohmann::json::parse(response);
    if (j.contains("node")) {
      CollectLeafNodes(j["node"], instances);
    }
  } catch (const std::exception& e) {
    LogError(std::string("[etcd] parse list response failed: ") + e.what());
    return false;
  }
  return !instances->empty();
}

bool EtcdClient::GetFirstServiceAddress(const std::string& service,
                                        std::string* address) {
  std::unordered_map<std::string, std::string> instances;
  if (!ListServiceAddresses(service, &instances) || instances.empty()) {
    return false;
  }

  std::vector<std::string> ids;
  ids.reserve(instances.size());
  for (const auto& kv : instances) ids.push_back(kv.first);
  std::sort(ids.begin(), ids.end());
  if (address) *address = instances[ids.front()];
  return true;
}

EtcdServiceRegistrar::EtcdServiceRegistrar(std::shared_ptr<EtcdClient> client,
                                           std::string service,
                                           std::string instance,
                                           std::string address,
                                           int ttl_seconds)
    : client_(std::move(client)),
      service_(std::move(service)),
      instance_(std::move(instance)),
      address_(std::move(address)),
      ttl_seconds_(ttl_seconds > 0 ? ttl_seconds : 15) {}

EtcdServiceRegistrar::~EtcdServiceRegistrar() {
  Stop();
}

bool EtcdServiceRegistrar::Start() {
  if (!client_ || !client_->Enabled() || service_.empty() || instance_.empty() ||
      address_.empty()) {
    return false;
  }
  if (!client_->PutService(service_, instance_, address_, ttl_seconds_)) {
    LogError("[etcd] register service failed: " + service_ + "/" + instance_);
    return false;
  }

  running_ = true;
  heartbeat_thread_ = std::thread(&EtcdServiceRegistrar::HeartbeatLoop, this);
  LogInfo("[etcd] registered " + service_ + "/" + instance_ + " -> " + address_);
  return true;
}

void EtcdServiceRegistrar::Stop() {
  if (!running_) return;
  running_ = false;
  if (heartbeat_thread_.joinable()) {
    heartbeat_thread_.join();
  }
  if (client_) {
    client_->DeleteService(service_, instance_);
  }
}

void EtcdServiceRegistrar::HeartbeatLoop() {
  const int sleep_seconds = std::max(1, ttl_seconds_ / 2);
  while (running_) {
    std::this_thread::sleep_for(std::chrono::seconds(sleep_seconds));
    if (!running_) break;
    client_->PutService(service_, instance_, address_, ttl_seconds_);
  }
}

std::shared_ptr<EtcdClient> CreateEtcdClient(const Config& cfg) {
  if (!cfg.enable_etcd || cfg.etcd_endpoints.empty()) return nullptr;
  auto client = std::make_shared<EtcdClient>(cfg.etcd_endpoints, cfg.etcd_prefix);
  return client->Enabled() ? client : nullptr;
}

std::string ResolveLogicGrpcTarget(const Config& cfg) {
  std::string target = cfg.logic_grpc_target;
  auto client = CreateEtcdClient(cfg);
  if (!client) return target;

  std::string discovered;
  if (client->GetFirstServiceAddress("logic", &discovered)) {
    LogInfo("[etcd] discovered logic target: " + discovered);
    return discovered;
  }
  if (!target.empty()) {
    LogInfo("[etcd] logic discovery miss, fallback to static target: " + target);
  }
  return target;
}

bool DiscoverCometGrpcTargets(const Config& cfg,
                              std::unordered_map<std::string, std::string>* targets) {
  auto client = CreateEtcdClient(cfg);
  if (!client || !targets) return false;

  if (client->ListServiceAddresses("comet", targets)) {
    LogInfo("[etcd] discovered " + std::to_string(targets->size()) +
            " comet targets");
    return true;
  }
  return false;
}

}  // namespace sparkpush
