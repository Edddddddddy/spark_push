#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
RELEASE_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"

: "${APP_IMAGE_REPOSITORY:?APP_IMAGE_REPOSITORY is required}"
: "${APP_IMAGE_TAG:?APP_IMAGE_TAG is required}"

DEPLOY_ENV="${DEPLOY_ENV:-prod}"
APP_ROOT="${APP_ROOT:-/opt/apps/spark_push}"
PROJECT_NAME="${PROJECT_NAME:-spark_push_${DEPLOY_ENV}}"

ENV_ROOT="${APP_ROOT}/${DEPLOY_ENV}"
SHARED_DIR="${APP_ROOT}/shared/${DEPLOY_ENV}"
ENV_FILE="${ENV_FILE:-${SHARED_DIR}/env/app.env}"
OVERRIDE_FILE="${OVERRIDE_FILE:-${SHARED_DIR}/compose/docker-compose.override.yml}"
CURRENT_LINK="${ENV_ROOT}/current"
COMPOSE_FILE="${RELEASE_DIR}/deploy/docker-compose.release.yml"

if [[ ! "$DEPLOY_ENV" =~ ^(dev|test|prod)$ ]]; then
  echo "DEPLOY_ENV must be one of: dev, test, prod" >&2
  exit 2
fi

if [[ ! -f "$ENV_FILE" ]]; then
  echo "Missing environment file: $ENV_FILE" >&2
  echo "Create it from deploy/.env.example and protect it with chmod 600." >&2
  exit 2
fi

mkdir -p \
  "$ENV_ROOT/releases" \
  "$SHARED_DIR/compose" \
  "$SHARED_DIR/certs" \
  "$SHARED_DIR/logs" \
  "$SHARED_DIR/backups"

upsert_env() {
  local key="$1"
  local value="$2"

  if grep -qE "^${key}=" "$ENV_FILE"; then
    sed -i "s|^${key}=.*|${key}=${value}|" "$ENV_FILE"
  else
    printf '%s=%s\n' "$key" "$value" >>"$ENV_FILE"
  fi
}

ensure_override() {
  if [[ -f "$OVERRIDE_FILE" ]]; then
    return
  fi

  cat >"$OVERRIDE_FILE" <<EOF
services:
  mysql:
    container_name: spark-push-${DEPLOY_ENV}-mysql
  redis:
    container_name: spark-push-${DEPLOY_ENV}-redis
  etcd:
    container_name: spark-push-${DEPLOY_ENV}-etcd
  kafka:
    container_name: spark-push-${DEPLOY_ENV}-kafka
  kafka-init:
    container_name: spark-push-${DEPLOY_ENV}-kafka-init
  logic:
    container_name: spark-push-${DEPLOY_ENV}-logic
    pull_policy: \${APP_PULL_POLICY:-always}
  comet:
    container_name: spark-push-${DEPLOY_ENV}-comet
    pull_policy: \${APP_PULL_POLICY:-always}
  job:
    container_name: spark-push-${DEPLOY_ENV}-job
    pull_policy: \${APP_PULL_POLICY:-always}
  web-demo:
    container_name: spark-push-${DEPLOY_ENV}-web-demo
    pull_policy: \${APP_PULL_POLICY:-always}
  nginx:
    container_name: spark-push-${DEPLOY_ENV}-nginx
EOF
}

upsert_env "APP_IMAGE_REPOSITORY" "$APP_IMAGE_REPOSITORY"
upsert_env "APP_IMAGE_TAG" "$APP_IMAGE_TAG"
upsert_env "TLS_CERT_HOST_DIR" "${TLS_CERT_HOST_DIR:-${SHARED_DIR}/certs}"

cat >"${RELEASE_DIR}/.release.env" <<EOF
APP_ROOT=${APP_ROOT}
DEPLOY_ENV=${DEPLOY_ENV}
APP_IMAGE_REPOSITORY=${APP_IMAGE_REPOSITORY}
APP_IMAGE_TAG=${APP_IMAGE_TAG}
PROJECT_NAME=${PROJECT_NAME}
EOF

ensure_override

if [[ -n "${GHCR_USERNAME:-}" && -n "${GHCR_TOKEN:-}" ]]; then
  printf '%s' "$GHCR_TOKEN" | docker login ghcr.io -u "$GHCR_USERNAME" --password-stdin
fi

compose() {
  docker compose \
    --env-file "$ENV_FILE" \
    -p "$PROJECT_NAME" \
    -f "$COMPOSE_FILE" \
    -f "$OVERRIDE_FILE" \
    "$@"
}

cd "$RELEASE_DIR"
if [[ "${SKIP_PULL:-false}" != "true" ]]; then
  compose pull
fi
compose up -d --remove-orphans

if command -v curl >/dev/null 2>&1; then
  public_http_port="$(awk -F= '/^PUBLIC_HTTP_PORT=/{print $2; exit}' "$ENV_FILE")"
  health_url="${HEALTH_URL:-http://127.0.0.1:${public_http_port:-80}/health}"
  for _ in $(seq 1 30); do
    if curl -fsS "$health_url" >/dev/null; then
      ln -sfn "$RELEASE_DIR" "$CURRENT_LINK"
      echo "Health check passed: $health_url"
      exit 0
    fi
    sleep 3
  done
  echo "Health check failed: $health_url" >&2
  compose ps
  exit 1
fi

ln -sfn "$RELEASE_DIR" "$CURRENT_LINK"
