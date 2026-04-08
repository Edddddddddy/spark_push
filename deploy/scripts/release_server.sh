#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
ENV_FILE="$REPO_ROOT/deploy/.env.prod"
COMPOSE_FILE="$REPO_ROOT/deploy/docker-compose.release.yml"

: "${APP_IMAGE_REPOSITORY:?APP_IMAGE_REPOSITORY is required}"
: "${APP_IMAGE_TAG:?APP_IMAGE_TAG is required}"

if [[ ! -f "$ENV_FILE" ]]; then
  cp "$REPO_ROOT/deploy/.env.example" "$ENV_FILE"
fi

upsert_env() {
  local key="$1"
  local value="$2"

  if grep -qE "^${key}=" "$ENV_FILE"; then
    sed -i "s|^${key}=.*|${key}=${value}|" "$ENV_FILE"
  else
    printf '%s=%s\n' "$key" "$value" >>"$ENV_FILE"
  fi
}

upsert_env "APP_IMAGE_REPOSITORY" "$APP_IMAGE_REPOSITORY"
upsert_env "APP_IMAGE_TAG" "$APP_IMAGE_TAG"

if [[ -n "${GHCR_USERNAME:-}" && -n "${GHCR_TOKEN:-}" ]]; then
  printf '%s' "$GHCR_TOKEN" | docker login ghcr.io -u "$GHCR_USERNAME" --password-stdin
fi

cd "$REPO_ROOT"
docker compose --env-file "$ENV_FILE" -f "$COMPOSE_FILE" pull
docker compose --env-file "$ENV_FILE" -f "$COMPOSE_FILE" up -d

