# NativeLink server + worker for Zitter's LAN remote builds.
# Compiles run with the Linux clang that Siso sends with each action, so the
# image only needs bash, coreutils and glibc.
FROM ubuntu:24.04

ARG NATIVELINK_VERSION=1.8.3
RUN apt-get update \
 && apt-get install -y --no-install-recommends ca-certificates curl \
 && rm -rf /var/lib/apt/lists/* \
 && mkdir /tmp/nl \
 && curl -fsSL "https://github.com/TraceMachina/nativelink/releases/download/v${NATIVELINK_VERSION}/nativelink-${NATIVELINK_VERSION}-x86_64-unknown-linux-musl.tar.gz" \
    | tar -xz -C /tmp/nl \
 && find /tmp/nl -type f -name nativelink -exec install -m 755 {} /usr/local/bin/nativelink \; \
 && rm -rf /tmp/nl \
 && nativelink --version

COPY worker_action.sh /usr/local/bin/worker_action.sh
COPY nativelink_server.json5 /etc/nativelink/server.json5
RUN chmod 755 /usr/local/bin/worker_action.sh && mkdir -p /data

EXPOSE 50051 50061
ENTRYPOINT ["nativelink", "/etc/nativelink/server.json5"]
