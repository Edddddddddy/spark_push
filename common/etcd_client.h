#pragma once

#include "config.h"

#include <memory>
#include <string>
#include <thread>
#include <unordered_map>

namespace sparkpush {

class EtcdClient {
 public:
  EtcdClient(std::string endpoints, std::string prefix);

  bool Enabled() const;
  bool PutService(const std::string& service,
                  const std::string& instance,
                  const std::string& value,
                  int ttl_seconds);
  bool DeleteService(const std::string& service, const std::string& instance);
  bool GetFirstServiceAddress(const std::string& service, std::string* address);
  bool ListServiceAddresses(
      const std::string& service,
      std::unordered_map<std::string, std::string>* instances);

 private:
  bool Request(const std::string& method,
               const std::string& path,
               const std::string& body,
               std::string* response_body,
               long* status_code);
  std::string BuildServicePath(const std::string& service,
                               const std::string& instance = "") const;
  std::string endpoint_;
  std::string prefix_;
};

class EtcdServiceRegistrar {
 public:
  EtcdServiceRegistrar(std::shared_ptr<EtcdClient> client,
                       std::string service,
                       std::string instance,
                       std::string address,
                       int ttl_seconds);
  ~EtcdServiceRegistrar();

  bool Start();
  void Stop();

 private:
  void HeartbeatLoop();

  std::shared_ptr<EtcdClient> client_;
  std::string service_;
  std::string instance_;
  std::string address_;
  int ttl_seconds_{15};
  bool running_{false};
  std::thread heartbeat_thread_;
};

std::shared_ptr<EtcdClient> CreateEtcdClient(const Config& cfg);
std::string ResolveLogicGrpcTarget(const Config& cfg);
bool DiscoverCometGrpcTargets(
    const Config& cfg,
    std::unordered_map<std::string, std::string>* targets);

}  // namespace sparkpush
