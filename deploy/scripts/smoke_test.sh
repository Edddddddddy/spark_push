#!/usr/bin/env bash
set -euo pipefail

BASE_URL="${1:-http://127.0.0.1}"

echo "[1/3] Checking reverse proxy..."
curl -fsS "$BASE_URL/" >/dev/null

echo "[2/3] Checking logic health..."
curl -fsS "$BASE_URL/health"
echo

echo "[3/3] Checking static home page..."
curl -fsS "$BASE_URL/index.html" >/dev/null

echo "Smoke test passed for $BASE_URL"
