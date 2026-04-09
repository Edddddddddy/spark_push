#include "app.h"

#include "comet_grpc_service.h"
#include "comet_server.h"
#include "etcd_client.h"
#include "logging.h"
#include "signal_handler.h"

#include <grpcpp/grpcpp.h>

#include <thread>

namespace sparkpush {

void RunComet(const Config& cfg) {
  Config effective_cfg = cfg;
  effective_cfg.logic_grpc_target = ResolveLogicGrpcTarget(cfg);

  EventLoop loop;
  CometServer server(&loop, effective_cfg);
  server.SetThreadNum(effective_cfg.comet_io_threads);
  server.Start();
  LogInfo("Comet server listening on port " +
          std::to_string(effective_cfg.listen_port));

  // gRPC 服务器（用于 job 推送）
  std::string addr =
      effective_cfg.listen_addr + ":" + std::to_string(effective_cfg.comet_grpc_port);
  grpc::ServerBuilder builder;
  CometServiceImpl grpcService(&server);
  builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
  builder.RegisterService(&grpcService);
  std::unique_ptr<grpc::Server> grpcServer = builder.BuildAndStart();
  LogInfo("Comet gRPC server listening on " + addr);

  auto etcd_client = CreateEtcdClient(effective_cfg);
  std::unique_ptr<EtcdServiceRegistrar> registrar;
  if (etcd_client) {
    std::string advertise_addr = effective_cfg.comet_advertise_addr;
    if (advertise_addr.empty()) {
      advertise_addr = "comet:" + std::to_string(effective_cfg.comet_grpc_port);
    }
    registrar = std::make_unique<EtcdServiceRegistrar>(
        etcd_client,
        "comet",
        effective_cfg.comet_id,
        advertise_addr,
        effective_cfg.etcd_lease_ttl);
    registrar->Start();
  }

  // 优雅关闭：收到 SIGTERM/SIGINT 后退出 EventLoop 并关闭 gRPC
  InstallSignalHandler([&loop, &grpcServer]() {
    // quit() 是线程安全的，会唤醒 EventLoop
    loop.quit();
    if (grpcServer) {
      grpcServer->Shutdown();
    }
  });

  // gRPC 使用单独线程阻塞 Wait，muduo EventLoop 在当前线程运行
  std::thread grpc_thread([&grpcServer]() {
    grpcServer->Wait();
  });

  // 必须在创建 EventLoop 的线程中调用 loop()
  loop.loop();

  // 优雅关闭序列
  LogInfo("Comet shutting down gracefully...");
  if (grpcServer) {
    grpcServer->Shutdown();
  }
  if (grpc_thread.joinable()) {
    grpc_thread.join();
  }
  if (registrar) {
    registrar->Stop();
  }
  LogInfo("Comet shutdown complete");
}

}  // namespace sparkpush



