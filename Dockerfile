FROM alpine:3.21 AS builder

RUN apk add --no-cache \
    build-base \
    autoconf \
    automake \
    openssl-dev \
    openssl-libs-static \
    openssl \
    pkgconf

WORKDIR /src
COPY . .

RUN ./autogen.sh \
    && ./configure LDFLAGS="-static" \
    && make tiny

# Extract only the CA certificates in Discord's chain
RUN echo | openssl s_client -connect gateway.discord.gg:443 -showcerts 2>/dev/null \
    | awk '/BEGIN CERTIFICATE/,/END CERTIFICATE/{ print }' > /discord-ca.crt \
    && test -s /discord-ca.crt

FROM scratch

COPY --from=builder /src/src/discord2 /discord2
COPY --from=builder /discord-ca.crt /etc/ssl/certs/ca-certificates.crt

ENTRYPOINT ["/discord2"]
