# FILE Group Engine — Docker image

FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake libssl-dev libgrpc++-dev protobuf-compiler-grpc \
    libprotobuf-dev ca-certificates && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /build
COPY . .

RUN mkdir -p build && cd build && cmake .. && make engine_grpc -j$(nproc)

RUN mkdir -p /var/lib/filegroup /tmp/filegroup

EXPOSE 8443
ENTRYPOINT ["./build/engine_grpc"]
