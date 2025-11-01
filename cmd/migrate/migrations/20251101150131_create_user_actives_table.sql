-- +goose Up
-- +goose StatementBegin
CREATE TABLE IF NOT EXISTS user_actives (
    id SERIAL PRIMARY KEY,
    created_at TIMESTAMP NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMP NOT NULL DEFAULT NOW(),
    user_uid BIGINT NOT NULL UNIQUE REFERENCES user_cores(user_uid) ON DELETE CASCADE,
    online BOOLEAN DEFAULT TRUE
);

CREATE OR REPLACE FUNCTION set_updated_at_user_actives()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = NOW();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER set_updated_at_user_actives_trigger
    BEFORE UPDATE ON user_actives
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at_user_actives();
-- +goose StatementEnd

-- +goose Down
-- +goose StatementBegin
DROP TRIGGER IF EXISTS set_updated_at_user_actives_trigger ON user_actives;
DROP FUNCTION IF EXISTS set_updated_at_user_actives();
DROP TABLE IF EXISTS user_actives;
-- +goose StatementEnd
