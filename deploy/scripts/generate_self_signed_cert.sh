#!/usr/bin/env bash
set -euo pipefail

CERT_DIR="${1:-deploy/nginx/certs}"
HOST_NAME="${2:-47.250.130.135}"

mkdir -p "$CERT_DIR"

if [[ "$HOST_NAME" =~ ^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
  SAN="IP:${HOST_NAME},DNS:localhost"
else
  SAN="DNS:${HOST_NAME},DNS:localhost"
fi

openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
  -keyout "${CERT_DIR}/privkey.pem" \
  -out "${CERT_DIR}/fullchain.pem" \
  -subj "/CN=${HOST_NAME}" \
  -addext "subjectAltName=${SAN}"

echo "Generated self-signed certificate in ${CERT_DIR}"
