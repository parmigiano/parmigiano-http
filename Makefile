DB_URL ?= postgres://postgres:password@localhost:5432/parmigiano?sslmode=disable
MIGRATIONS_DIR ?= cmd/migrate/migrations

GOOSE ?= goose

PORT ?= 8080
BINARY_MAIN := bin/server-http
BINARY_RESERV := bin/server-http-reserv

.PHONY: fmt lint check build-clean build test migrate-create migrate-up migrate-down

fmt:
	gofmt -w .
	goimports -w .

lint:
	golangci-lint run

check: fmt lint test
	@echo "==> All checks passed!"

build:
	@echo "==> Building main server (8080)..."
	go build -ldflags="-s -w \
		-X parmigiano/http/config.HttpServerPort=8080" \
		-o $(BINARY_MAIN) ./cmd/server

	@echo "==> Building backup server (8181)..."
	go build -ldflags="-s -w \
		-X parmigiano/http/config.HttpServerPort=8181" \
		-o $(BINARY_RESERV) ./cmd/server

test:
	@go test -v ./...

run: build
	@./bin/server-http

run-reserv: build
	@./bin/server-http-reserv

run-to-test:
	@cmd /c "$(CURDIR)/$(BATCH_FILE_TEST)"

## make migrate-create NAME=create_users_table
migrate-create:
	@echo "==> Creating new migration: $(NAME)"
	$(GOOSE) -dir $(MIGRATIONS_DIR) create $(NAME) sql

migrate-up:
	@echo "==> Running up migrations"
	$(GOOSE) -dir $(MIGRATIONS_DIR) postgres "$(DB_URL)" up

migrate-down:
	@echo "==> Running down migrations..."
	$(GOOSE) -dir $(MIGRATIONS_DIR) postgres "$(DB_URL)" down

database-drop:
	@echo "==> Dropping all tables in database..."
	"D:\Program Files\PostgreSQL\17\bin\psql.exe" "$(DB_URL)" -c "DROP SCHEMA public CASCADE; CREATE SCHEMA public;"
