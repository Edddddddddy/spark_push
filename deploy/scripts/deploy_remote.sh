#!/usr/bin/env bash
set -euo pipefail

REMOTE_HOST="${1:?usage: deploy_remote.sh user@host [/opt/spark-push]}"
REMOTE_DIR="${2:-/opt/spark-push}"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

rsync -az --delete \
  --exclude '.git' \
  --exclude 'build' \
  --exclude 'logs' \
  --exclude 'deploy/.env.prod' \
  "$REPO_ROOT/" "$REMOTE_HOST:$REMOTE_DIR/"

ssh "$REMOTE_HOST" "cd '$REMOTE_DIR' && test -f deploy/.env.prod || cp deploy/.env.example deploy/.env.prod"
ssh "$REMOTE_HOST" "cd '$REMOTE_DIR' && docker compose --env-file deploy/.env.prod -f deploy/docker-compose.prod.yml up -d --build"

echo "Remote deployment completed: $REMOTE_HOST:$REMOTE_DIR"
