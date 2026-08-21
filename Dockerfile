FROM ubuntu:24.04 AS build

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    libssl-dev \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

# BUILD_TESTS=OFF: this image is for running nodes, not running the test
# suite -- skips pulling in gtest and cuts build time significantly.
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF \
    && cmake --build build --target peer_a -j"$(nproc)"

FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 \
    netcat-openbsd \
    && rm -rf /var/lib/apt/lists/*

COPY --from=build /src/build/src/app/peer_a /usr/local/bin/forgechain-node

ENTRYPOINT ["forgechain-node"]
