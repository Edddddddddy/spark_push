#!/usr/bin/env bash
set -euo pipefail

APP_ROOT="${APP_ROOT:-/opt/apps/spark_push}"
DEPLOY_ENV="${1:-}"
ACTION="${2:-ps}"
ARG="${3:-}"

usage() {
  echo "Usage: $0 <dev|test|prod> <ps|logs|up|down|restart|config|rollback> [release-id]" >&2
}

if [[ ! "$DEPLOY_ENV" =~ ^(dev|test|prod)$ ]]; then
  usage
  exit 1
fi

ENV_ROOT="${APP_ROOT}/${DEPLOY_ENV}"
SHARED_DIR="${APP_ROOT}/shared/${DEPLOY_ENV}"
ENV_FILE="${SHARED_DIR}/env/app.env"
OVERRIDE_FILE="${SHARED_DIR}/compose/docker-compose.override.yml"
CURRENT_LINK="${ENV_ROOT}/current"
PROJECT_NAME="spark_push_${DEPLOY_ENV}"

current_release() {
  if [[ ! -L "$CURRENT_LINK" && ! -d "$CURRENT_LINK" ]]; then
    echo "No current release for ${DEPLOY_ENV}: ${CURRENT_LINK}" >&2
    exit 2
  fi
  cd "$CURRENT_LINK"
}

compose() {
  docker compose \
    --env-file "$ENV_FILE" \
    -p "$PROJECT_NAME" \
    -f "$PWD/deploy/docker-compose.release.yml" \
    -f "$OVERRIDE_FILE" \
    "$@"
}

case "$ACTION" in
  ps)
    current_release
    compose ps
    ;;
  logs)
    current_release
    compose logs -f --tail=200
    ;;
  up)
    current_release
    if [[ -f .release.env ]]; then
      # shellcheck disable=SC1091
      source .release.env
    fi
    APP_ROOT="$APP_ROOT" DEPLOY_ENV="$DEPLOY_ENV" bash deploy/scripts/release_server.sh
    ;;
  down)
    current_release
    compose down
    ;;
  restart)
    current_release
    compose restart
    ;;
  config)
    current_release
    compose config
    ;;
  rollback)
    if [[ -z "$ARG" ]]; then
      usage
      exit 1
    fi
    target="${ENV_ROOT}/releases/${ARG}"
    if [[ ! -d "$target" || ! -f "$target/.release.env" ]]; then
      echo "Release not found or missing metadata: $target" >&2
      exit 2
    fi
    cd "$target"
    # shellcheck disable=SC1091
    source .release.env
    APP_ROOT="$APP_ROOT" DEPLOY_ENV="$DEPLOY_ENV" bash deploy/scripts/release_server.sh
    ;;
  *)
    usage
    exit 1
    ;;
esac
