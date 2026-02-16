FROM alpine:3.21 AS builder

RUN apk add --no-cache \
    build-base \
    autoconf \
    automake

WORKDIR /src
COPY . .

RUN ./autogen.sh \
    && ./configure LDFLAGS="-static" \
    && make tiny

FROM scratch

COPY --from=builder /src/src/discord2 /discord2

ENTRYPOINT ["/discord2"]
