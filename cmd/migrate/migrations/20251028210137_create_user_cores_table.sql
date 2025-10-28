-- +goose Up
-- +goose StatementBegin
CREATE TABLE IF NOT EXISTS user_cores (
    id SERIAL PRIMARY KEY,
    created_at TIMESTAMP NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMP NOT NULL DEFAULT NOW(),
    user_uuid VARCHAR(255) NOT NULL UNIQUE,
    email VARCHAR(100) NOT NULL UNIQUE,
    password VARCHAR(255) NOT NULL,
    access_token VARCHAR(255) NOT NULL UNIQUE,
    refresh_token VARCHAR(255) UNIQUE
);

CREATE OR REPLACE FUNCTION set_updated_at_user_cores()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = NOW();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER set_updated_at_user_cores_trigger
    BEFORE UPDATE ON user_cores
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at_user_cores();
-- +goose StatementEnd

-- +goose Down
-- +goose StatementBegin
DROP TRIGGER IF EXISTS set_updated_at_user_cores_trigger ON user_cores;
DROP FUNCTION IF EXISTS set_updated_at_user_cores();
DROP TABLE IF EXISTS user_cores;
-- +goose StatementEnd
