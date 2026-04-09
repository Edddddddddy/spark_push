#!/usr/bin/env bash
set -euo pipefail

APP_ROOT="/app"
CONF_DIR="$APP_ROOT/run/conf"

mkdir -p "$CONF_DIR" "$APP_ROOT/logs"

write_logic_conf() {
  cat >"$CONF_DIR/logic.conf" <<EOF
listen_addr=0.0.0.0
listen_port=${LOGIC_GRPC_PORT:-9100}
http_port=${LOGIC_HTTP_PORT:-9101}
kafka_brokers=${KAFKA_BROKERS:-kafka:9092}
kafka_push_topic=${KAFKA_PUSH_TOPIC:-push_to_comet}
kafka_broadcast_topic=${KAFKA_BROADCAST_TOPIC:-broadcast_task}
redis_host=${REDIS_HOST:-redis}
redis_port=${REDIS_PORT:-6379}
redis_password=${REDIS_PASSWORD:-}
redis_db=${REDIS_DB:-0}
redis_pool_size=${REDIS_POOL_SIZE:-16}
enable_etcd=${ENABLE_ETCD:-false}
etcd_endpoints=${ETCD_ENDPOINTS:-http://etcd:2379}
etcd_prefix=${ETCD_PREFIX:-/sparkpush/services}
etcd_lease_ttl=${ETCD_LEASE_TTL:-15}
logic_advertise_addr=${LOGIC_ADVERTISE_ADDR:-logic:${LOGIC_GRPC_PORT:-9100}}
mysql_host=${MYSQL_HOST:-mysql}
mysql_port=${MYSQL_PORT:-3306}
mysql_user=${MYSQL_USER:-spark_push}
mysql_password=${MYSQL_PASSWORD:-spark_push}
mysql_db=${MYSQL_DATABASE:-spark_push}
mysql_pool_size=${MYSQL_POOL_SIZE:-16}
http_threads=${LOGIC_HTTP_THREADS:-4}
EOF
}

write_comet_conf() {
  cat >"$CONF_DIR/comet.conf" <<EOF
listen_addr=0.0.0.0
listen_port=${COMET_WS_PORT:-9200}
comet_grpc_port=${COMET_GRPC_PORT:-9205}
logic_grpc_target=${LOGIC_GRPC_TARGET:-logic:9100}
comet_id=${COMET_ID:-comet-1}
comet_io_threads=${COMET_IO_THREADS:-8}
comet_grpc_pool_size=${COMET_GRPC_POOL_SIZE:-4}
enable_etcd=${ENABLE_ETCD:-false}
etcd_endpoints=${ETCD_ENDPOINTS:-http://etcd:2379}
etcd_prefix=${ETCD_PREFIX:-/sparkpush/services}
etcd_lease_ttl=${ETCD_LEASE_TTL:-15}
comet_advertise_addr=${COMET_ADVERTISE_ADDR:-comet:${COMET_GRPC_PORT:-9205}}
use_grpc_stream=${USE_GRPC_STREAM:-true}
grpc_stream_count=${GRPC_STREAM_COUNT:-4}
EOF
}

write_job_conf() {
  cat >"$CONF_DIR/job.conf" <<EOF
kafka_brokers=${KAFKA_BROKERS:-kafka:9092}
kafka_consumer_group=${KAFKA_CONSUMER_GROUP:-spark_push_group}
kafka_push_topic=${KAFKA_PUSH_TOPIC:-push_to_comet}
kafka_broadcast_topic=${KAFKA_BROADCAST_TOPIC:-broadcast_task}
comet_targets=${COMET_TARGETS:-comet-1=comet:9205}
use_push_stream=${USE_PUSH_STREAM:-false}
enable_etcd=${ENABLE_ETCD:-false}
etcd_endpoints=${ETCD_ENDPOINTS:-http://etcd:2379}
etcd_prefix=${ETCD_PREFIX:-/sparkpush/services}
etcd_lease_ttl=${ETCD_LEASE_TTL:-15}
mysql_host=${MYSQL_HOST:-mysql}
mysql_port=${MYSQL_PORT:-3306}
mysql_user=${MYSQL_USER:-spark_push}
mysql_password=${MYSQL_PASSWORD:-spark_push}
mysql_db=${MYSQL_DATABASE:-spark_push}
mysql_pool_size=${JOB_MYSQL_POOL_SIZE:-8}
job_rpc_worker_threads=${JOB_RPC_WORKER_THREADS:-8}
EOF
}

case "${SPARK_SERVICE:-}" in
  logic)
    write_logic_conf
    exec "$APP_ROOT/bin/logic_server" "$CONF_DIR/logic.conf"
    ;;
  comet)
    write_comet_conf
    exec "$APP_ROOT/bin/comet_server" "$CONF_DIR/comet.conf"
    ;;
  job)
    write_job_conf
    exec "$APP_ROOT/bin/job_server" "$CONF_DIR/job.conf"
    ;;
  web-demo)
    exec "$APP_ROOT/bin/web_demo_server" "${WEB_DEMO_PORT:-9001}" "$APP_ROOT/web_demo/static"
    ;;
  *)
    echo "Unknown or missing SPARK_SERVICE: ${SPARK_SERVICE:-}" >&2
    echo "Expected one of: logic, comet, job, web-demo" >&2
    exit 1
    ;;
esac
