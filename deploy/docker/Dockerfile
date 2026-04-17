FROM ubuntu:22.04 AS builder

ARG DEBIAN_FRONTEND=noninteractive
ARG UBUNTU_MIRROR=http://archive.ubuntu.com/ubuntu
ARG UBUNTU_SECURITY_MIRROR=http://security.ubuntu.com/ubuntu

RUN sed -i \
      -e "s|http://archive.ubuntu.com/ubuntu|${UBUNTU_MIRROR}|g" \
      -e "s|http://security.ubuntu.com/ubuntu|${UBUNTU_SECURITY_MIRROR}|g" \
      /etc/apt/sources.list \
 && printf 'Acquire::ForceIPv4 "true";\nAcquire::Retries "3";\n' >/etc/apt/apt.conf.d/99ci-network

RUN apt-get update && apt-get install -y --no-install-recommends \
    bash \
    build-essential \
    ca-certificates \
    cmake \
    curl \
    libcurl4-openssl-dev \
    default-libmysqlclient-dev \
    libc-ares-dev \
    libgrpc++-dev \
    libgrpc-dev \
    libhiredis-dev \
    libprotobuf-dev \
    librdkafka-dev \
    libssl-dev \
    nlohmann-json3-dev \
    pkg-config \
    protobuf-compiler \
    protobuf-compiler-grpc \
    zlib1g-dev \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
 && cmake --build build -j"$(nproc)"

FROM ubuntu:22.04 AS runtime

ARG DEBIAN_FRONTEND=noninteractive
ARG UBUNTU_MIRROR=http://archive.ubuntu.com/ubuntu
ARG UBUNTU_SECURITY_MIRROR=http://security.ubuntu.com/ubuntu

RUN sed -i \
      -e "s|http://archive.ubuntu.com/ubuntu|${UBUNTU_MIRROR}|g" \
      -e "s|http://security.ubuntu.com/ubuntu|${UBUNTU_SECURITY_MIRROR}|g" \
      /etc/apt/sources.list \
 && printf 'Acquire::ForceIPv4 "true";\nAcquire::Retries "3";\n' >/etc/apt/apt.conf.d/99ci-network

RUN apt-get update && apt-get install -y --no-install-recommends \
    bash \
    ca-certificates \
    curl \
    libcurl4 \
    default-libmysqlclient-dev \
    libc-ares-dev \
    libgrpc++-dev \
    libprotobuf-dev \
    libhiredis-dev \
    librdkafka-dev \
    libssl-dev \
    nlohmann-json3-dev \
    zlib1g \
 && rm -rf /var/lib/apt/lists/*

RUN useradd --create-home --home-dir /app --shell /bin/bash appuser

WORKDIR /app

COPY --from=builder /src/build/logic/logic_server /app/bin/logic_server
COPY --from=builder /src/build/comet/comet_server /app/bin/comet_server
COPY --from=builder /src/build/job/job_server /app/bin/job_server
COPY --from=builder /src/build/web_demo/web_demo_server /app/bin/web_demo_server
COPY --from=builder /src/web_demo/static /app/web_demo/static
COPY deploy/docker/entrypoint.sh /app/bin/entrypoint.sh

RUN chmod +x /app/bin/entrypoint.sh \
 && mkdir -p /app/logs /app/run/conf \
 && chown -R appuser:appuser /app

USER appuser

ENTRYPOINT ["/app/bin/entrypoint.sh"]
