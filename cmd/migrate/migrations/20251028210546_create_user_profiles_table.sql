-- +goose Up
-- +goose StatementBegin
CREATE TABLE IF NOT EXISTS user_profiles (
    id SERIAL PRIMARY KEY,
    created_at TIMESTAMP NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMP NOT NULL DEFAULT NOW(),
    user_uuid VARCHAR(255) NOT NULL UNIQUE REFERENCES user_cores(user_uuid) ON DELETE CASCADE,
    avatar VARCHAR(255),
    username VARCHAR(30) NOT NULL UNIQUE
);

CREATE OR REPLACE FUNCTION set_updated_at_user_profiles()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = NOW();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER set_updated_at_user_profiles_trigger
    BEFORE UPDATE ON user_profiles
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at_user_profiles();
-- +goose StatementEnd

-- +goose Down
-- +goose StatementBegin
DROP TRIGGER IF EXISTS set_updated_at_user_profiles_trigger ON user_profiles;
DROP FUNCTION IF EXISTS set_updated_at_user_profiles();
DROP TABLE IF EXISTS user_profiles;
-- +goose StatementEnd
