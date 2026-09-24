FROM node:20-bookworm-slim

WORKDIR /app

COPY package.json package-lock.json ./
RUN npm ci --omit=dev

COPY server.js healthcheck.js ./

ENV PORT=9090
ENV TF_CPP_MIN_LOG_LEVEL=2
EXPOSE 9090

CMD ["node", "server.js"]
