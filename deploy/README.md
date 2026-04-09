# Production Deployment

这套部署方案针对单机服务器做了“线上化”整理，核心目标是：

- `Docker Compose` 统一编排应用和中间件
- `Nginx` 作为单入口，统一代理静态页、HTTP API、WebSocket
- `systemd` 托管整套服务，支持开机自启
- 配置全部通过 `deploy/.env.prod` 管理
- MySQL / Redis / Kafka 使用持久化卷

## 1. 部署拓扑

- `nginx`：外部唯一入口，默认暴露 `80`
- `web-demo`：静态页面服务
- `logic`：HTTP API + gRPC
- `comet`：WebSocket 接入 + gRPC
- `job`：Kafka 消费、下行推送、异步持久化
- `mysql` / `redis` / `kafka`：基础依赖

对外默认只开放一个入口：

- `http://<server-ip>/`

Nginx 转发规则：

- `/` -> `web-demo`
- `/api/` -> `logic`
- `/ws` -> `comet`
- `/health` -> `logic`

## 2. 服务器准备

建议机器：

- Ubuntu 22.04
- 4C8G 起步
- 系统盘至少 40G

先安装 Docker：

```bash
chmod +x deploy/scripts/install-docker-ubuntu.sh
./deploy/scripts/install-docker-ubuntu.sh
```

## 3. 配置环境变量

复制一份生产环境配置：

```bash
cp deploy/.env.example deploy/.env.prod
```

至少要改这几个值：

- `MYSQL_ROOT_PASSWORD`
- `MYSQL_PASSWORD`
- `KAFKA_KRAFT_CLUSTER_ID`
- `PUBLIC_HTTP_PORT`

建议也按你的服务器实际资源调整：

- `MYSQL_POOL_SIZE`
- `REDIS_POOL_SIZE`
- `COMET_IO_THREADS`
- `JOB_RPC_WORKER_THREADS`

## 4. 启动

第一次启动：

```bash
docker compose --env-file deploy/.env.prod -f deploy/docker-compose.prod.yml up -d --build
```

查看状态：

```bash
docker compose --env-file deploy/.env.prod -f deploy/docker-compose.prod.yml ps
```

查看日志：

```bash
docker compose --env-file deploy/.env.prod -f deploy/docker-compose.prod.yml logs -f
```

## 5. 健康检查

基础健康检查：

```bash
curl http://127.0.0.1/health
```

或者直接跑脚本：

```bash
chmod +x deploy/scripts/smoke_test.sh
./deploy/scripts/smoke_test.sh http://127.0.0.1
```

## 6. 开机自启

把仓库放到固定目录，例如 `/opt/spark-push`，然后安装 systemd 单元：

```bash
sudo cp deploy/systemd/spark-push.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now spark-push
```

常用命令：

```bash
sudo systemctl status spark-push
sudo systemctl restart spark-push
sudo systemctl stop spark-push
```

## 7. 远程一键发布

如果本地已经配好了 SSH 免密，可以直接用：

```bash
chmod +x deploy/scripts/deploy_remote.sh
./deploy/scripts/deploy_remote.sh root@47.250.130.135 /opt/spark-push
```

脚本会做三件事：

1. `rsync` 同步代码到远端
2. 如果远端没有 `deploy/.env.prod`，先从模板拷一份
3. 在远端执行 `docker compose up -d --build`

## 8. 生产建议

当前方案已经比“手工起进程”稳很多，但如果你想再往真实线上靠，可以继续加：

- 域名 + HTTPS 证书
- 监控与告警，例如 Prometheus / Grafana / Loki
- MySQL 与 Kafka 独立到托管服务
- 多 Comet 节点横向扩容
- CI/CD 自动构建镜像并发布

## 9. HTTPS（当前已支持）

推荐把证书放在仓库外的运行时目录，避免后续代码同步把证书覆盖掉：

```bash
mkdir -p /opt/spark-push/.runtime/nginx-certs
bash deploy/scripts/generate_self_signed_cert.sh /opt/spark-push/.runtime/nginx-certs 47.250.130.135
```

然后在 `deploy/.env.prod` 里至少配置：

```bash
ENABLE_TLS=true
PUBLIC_HTTPS_PORT=443
TLS_CERT_HOST_DIR=/opt/spark-push/.runtime/nginx-certs
TLS_CERT_FILE=/etc/nginx/certs/fullchain.pem
TLS_KEY_FILE=/etc/nginx/certs/privkey.pem
```

重启：

```bash
docker compose --env-file deploy/.env.prod -f deploy/docker-compose.prod.yml up -d --build
```

验证：

```bash
curl -k https://47.250.130.135/health
```

## 10. etcd 服务发现（当前已支持）

当前实现已经支持：

- `logic` 启动后注册到 etcd
- `comet` 启动后注册到 etcd
- `comet` 优先从 etcd 发现 `logic`
- `job` 优先从 etcd 发现所有 `comet`
- 如果 etcd 暂时不可用，仍会回退到静态配置

`deploy/.env.prod` 里需要的关键配置：

```bash
ENABLE_ETCD=true
ETCD_ENDPOINTS=http://etcd:2379
ETCD_PREFIX=/sparkpush/services
ETCD_LEASE_TTL=15
LOGIC_ADVERTISE_ADDR=logic:9100
COMET_ADVERTISE_ADDR=comet:9205
```

验证：

```bash
docker exec spark-push-logic curl -fsS http://etcd:2379/v2/keys/sparkpush/services?recursive=true
```
