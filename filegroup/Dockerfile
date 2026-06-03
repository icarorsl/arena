# FILE Group Engine — Docker image
# Build:  docker build -t filegroup-engine -f Dockerfile ..
# Run:    docker run -p 8443:8443 filegroup-engine
# Test:   curl http://localhost:8443

FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake libssl-dev libgrpc++-dev protobuf-compiler-grpc \
    libprotobuf-dev libgtest-dev && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

RUN mkdir build && cd build && cmake .. && make engine_grpc -j$(nproc)

# ── Runtime ──────────────────────────────────────────────────────────────────
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 libgrpc++1.64 libprotobuf-lite-3.21 \
    ca-certificates && \
    rm -rf /var/lib/apt/lists/*

COPY --from=builder /build/build/engine_grpc /usr/local/bin/engine_grpc

RUN mkdir -p /var/lib/filegroup

EXPOSE 8443

ENTRYPOINT ["engine_grpc"]
