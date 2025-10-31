-- +goose Up
-- +goose StatementBegin
CREATE TABLE IF NOT EXISTS user_public_keys (
    id SERIAL PRIMARY KEY,
    created_at TIMESTAMP NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMP NOT NULL DEFAULT NOW(),
    user_uid BIGINT NOT NULL UNIQUE,
    public_key TEXT UNIQUE
);

CREATE OR REPLACE FUNCTION set_updated_at_user_public_keys()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = NOW();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER set_updated_at_user_public_keys_trigger
    BEFORE UPDATE ON user_public_keys
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at_user_public_keys();
-- +goose StatementEnd

-- +goose Down
-- +goose StatementBegin
DROP TRIGGER IF EXISTS set_updated_at_user_public_keys_trigger ON user_public_keys;
DROP FUNCTION IF EXISTS set_updated_at_user_public_keys();
DROP TABLE IF EXISTS user_public_keys;
-- +goose StatementEnd
