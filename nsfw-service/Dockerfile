FROM node:20-bookworm-slim

WORKDIR /app

COPY package.json package-lock.json ./
RUN npm ci --omit=dev

COPY server.js healthcheck.js ./

ENV PORT=8081
ENV TF_CPP_MIN_LOG_LEVEL=2
EXPOSE 8081

CMD ["node", "server.js"]
