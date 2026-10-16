FROM node:20-bookworm-slim

WORKDIR /app

COPY package.json package-lock.json ./
RUN set -eux; \
    npm config set fetch-retries 5; \
    npm config set fetch-retry-mintimeout 20000; \
    npm config set fetch-retry-maxtimeout 120000; \
    npm config set fetch-timeout 300000; \
    for i in 1 2 3; do \
        npm ci --omit=dev && exit 0; \
        echo "npm ci failed, attempt $i"; \
        sleep 15; \
    done; \
    exit 1

COPY server.js healthcheck.js ./

ENV PORT=9090
ENV TF_CPP_MIN_LOG_LEVEL=2
EXPOSE 9090

CMD ["node", "server.js"]
