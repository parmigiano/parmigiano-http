FROM parmigiano-http:auth-test-builder
RUN apt-get update && apt-get install -y --no-install-recommends redis-server && rm -rf /var/lib/apt/lists/*
COPY . /app
RUN gcc -g -O1 -ffunction-sections -fdata-sections -Iinclude -I/usr/local/include -I/usr/include/postgresql -x c tests/auth_security.c.in -x none src/handlers/v2/auth_handler.c src/utilities/password.c src/utilities/string.c src/utilities/validation.c src/utilities/JSON.c src/infra/store/redis/redis.c src/infra/store/redis/redis_email_confirm.c src/infra/store/redis/redis_verifycode.c src/infra/store/redis/redis_session.c src/infra/encryption/encryption.c -Wl,--gc-sections -lchttpx -lhiredis -largon2 -lcrypto -lcjson -lpthread -o /tmp/auth-security-test
RUN redis-server --daemonize yes --save "" --appendonly no && /tmp/auth-security-test
