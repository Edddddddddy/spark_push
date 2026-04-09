#!/bin/sh
set -eu

OUTPUT=/etc/nginx/conf.d/default.conf
ENABLE_TLS=${ENABLE_TLS:-false}
LOGIC_HTTP_PORT=${LOGIC_HTTP_PORT:-9101}
COMET_WS_PORT=${COMET_WS_PORT:-9200}
WEB_DEMO_PORT=${WEB_DEMO_PORT:-9001}
TLS_CERT_FILE=${TLS_CERT_FILE:-/etc/nginx/certs/fullchain.pem}
TLS_KEY_FILE=${TLS_KEY_FILE:-/etc/nginx/certs/privkey.pem}

cat_http_server() {
  cat <<EOF
server {
    listen 80;
    server_name _;

    client_max_body_size 10m;
    proxy_read_timeout 600s;
    proxy_send_timeout 600s;

    location = /health {
        proxy_pass http://logic:${LOGIC_HTTP_PORT}/health;
        proxy_http_version 1.1;
        proxy_set_header Host \$host;
        proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto \$scheme;
    }

    location /api/ {
        proxy_pass http://logic:${LOGIC_HTTP_PORT};
        proxy_http_version 1.1;
        proxy_set_header Host \$host;
        proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto \$scheme;
    }

    location /ws {
        proxy_pass http://comet:${COMET_WS_PORT};
        proxy_http_version 1.1;
        proxy_set_header Upgrade \$http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host \$host;
        proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto \$scheme;
    }

    location / {
        proxy_pass http://web-demo:${WEB_DEMO_PORT};
        proxy_http_version 1.1;
        proxy_set_header Host \$host;
        proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto \$scheme;
    }
}
EOF
}

if [ "$ENABLE_TLS" = "true" ] && [ -f "$TLS_CERT_FILE" ] && [ -f "$TLS_KEY_FILE" ]; then
  {
    cat_http_server
    cat <<EOF

server {
    listen 443 ssl http2;
    server_name _;

    ssl_certificate ${TLS_CERT_FILE};
    ssl_certificate_key ${TLS_KEY_FILE};
    ssl_session_timeout 1d;
    ssl_session_cache shared:SSL:10m;
    ssl_protocols TLSv1.2 TLSv1.3;
    ssl_prefer_server_ciphers off;

    client_max_body_size 10m;
    proxy_read_timeout 600s;
    proxy_send_timeout 600s;

    location = /health {
        proxy_pass http://logic:${LOGIC_HTTP_PORT}/health;
        proxy_http_version 1.1;
        proxy_set_header Host \$host;
        proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto https;
    }

    location /api/ {
        proxy_pass http://logic:${LOGIC_HTTP_PORT};
        proxy_http_version 1.1;
        proxy_set_header Host \$host;
        proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto https;
    }

    location /ws {
        proxy_pass http://comet:${COMET_WS_PORT};
        proxy_http_version 1.1;
        proxy_set_header Upgrade \$http_upgrade;
        proxy_set_header Connection "upgrade";
        proxy_set_header Host \$host;
        proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto https;
    }

    location / {
        proxy_pass http://web-demo:${WEB_DEMO_PORT};
        proxy_http_version 1.1;
        proxy_set_header Host \$host;
        proxy_set_header X-Forwarded-For \$proxy_add_x_forwarded_for;
        proxy_set_header X-Forwarded-Proto https;
    }
}
EOF
  } >"$OUTPUT"
else
  cat_http_server >"$OUTPUT"
fi
