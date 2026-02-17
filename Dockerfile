FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    make curl unzip ca-certificates git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN git submodule update --init && make

FROM scratch

COPY --from=builder /src/king /king

ENTRYPOINT ["/king"]
