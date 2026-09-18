FROM noneandundefined/libchttpx:latest AS libchttpx

FROM debian:bookworm-slim AS builder

RUN set -eux; \
    for i in 1 2 3; do \
        apt-get update && \
        apt-get install -y --no-install-recommends ca-certificates && \
        break || { [ "$i" -lt 3 ] || exit 1; sleep 10; }; \
    done; \
    rm -rf /var/lib/apt/lists/*

RUN set -eux; \
    for i in 1 2 3; do \
        apt-get update && \
        apt-get install -y --no-install-recommends \
            build-essential git curl \
            libpq-dev libmaxminddb-dev \
            libhiredis-dev libcurl4-openssl-dev libssl-dev \
            libargon2-dev uuid-dev libcjson-dev pkg-config \
            libxml2-dev librabbitmq-dev && \
        break || { [ "$i" -lt 3 ] || exit 1; sleep 10; }; \
    done; \
    rm -rf /var/lib/apt/lists/*

WORKDIR /usr/local/src

RUN git clone --depth 1 https://github.com/bji/libs3.git \
    && cd libs3 && make && make install

RUN cp /usr/local/src/libs3/build/lib/libs3.so.4 /usr/local/lib/ \
    && ln -sf /usr/local/lib/libs3.so.4 /usr/local/lib/libs3.so

COPY --from=libchttpx /usr/local/lib/libchttpx.so /usr/local/lib/
COPY --from=libchttpx /usr/local/lib/pkgconfig/libchttpx.pc /usr/local/lib/pkgconfig/
COPY --from=libchttpx /usr/local/include/libchttpx /usr/local/include/libchttpx
RUN ldconfig

RUN mkdir -p /usr/local/share/GeoIP \
    && curl -fsSL -o /usr/local/share/GeoIP/GeoLite2-Country.mmdb \
       https://github.com/P3TERX/GeoLite.mmdb/releases/latest/download/GeoLite2-Country.mmdb

WORKDIR /app

COPY . .

RUN make clean && make TARGET=server-http lin

FROM debian:bookworm-slim

WORKDIR /app

RUN set -eux; \
    for i in 1 2 3; do \
        apt-get update && \
        apt-get install -y --no-install-recommends ca-certificates && \
        break || { [ "$i" -lt 3 ] || exit 1; sleep 10; }; \
    done; \
    rm -rf /var/lib/apt/lists/*

RUN set -eux; \
    for i in 1 2 3; do \
        apt-get update && \
        apt-get install -y --no-install-recommends \
            libpq5 libcurl4 libssl3 libargon2-1 \
            libuuid1 libmaxminddb0 \
            libicu72 libxml2 libcjson1 libhiredis0.14 librabbitmq4 \
            ffmpeg && \
        break || { [ "$i" -lt 3 ] || exit 1; sleep 10; }; \
    done; \
    rm -rf /var/lib/apt/lists/*

COPY --from=builder /usr/local/lib/*.so* /usr/local/lib/
COPY --from=builder /usr/local/share/GeoIP/GeoLite2-Country.mmdb /usr/local/share/GeoIP/
COPY --from=builder /app/.build/server-http .
COPY --from=builder /app/docs ./docs
COPY --from=builder /app/src/infra ./src/infra

RUN ldconfig \
    && ldd /app/server-http > /tmp/server-http-libraries.txt \
    && cat /tmp/server-http-libraries.txt \
    && ! grep -q 'not found' /tmp/server-http-libraries.txt

ENV LD_LIBRARY_PATH=/usr/local/lib

EXPOSE 8080

CMD ["./server-http"]
