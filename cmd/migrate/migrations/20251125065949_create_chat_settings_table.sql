-- +goose Up
-- +goose StatementBegin
CREATE TABLE IF NOT EXISTS chat_settings (
    id SERIAL PRIMARY KEY,
    created_at TIMESTAMPTZ NOT NULL DEFAULT (timezone('UTC', now())),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT (timezone('UTC', now())),
    chat_id BIGINT REFERENCES chats(id) ON DELETE CASCADE,
    custom_background VARCHAR(255),
    blocked BOOLEAN NOT NULL DEFAULT FALSE,
    who_blocked_uid BIGINT NOT NULL DEFAULT 0
);

CREATE OR REPLACE FUNCTION set_updated_at_chat_settings()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = NOW();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER set_updated_at_chat_settings_trigger
    BEFORE UPDATE ON chat_settings
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at_chat_settings();

-- +INDEXES
CREATE INDEX IF NOT EXISTS idx_chat_settings_chat_id ON chat_settings(chat_id);
-- +INDEXES
-- +goose StatementEnd

-- +goose Down
-- +goose StatementBegin
-- -INDEXES
DROP INDEX IF EXISTS idx_chat_settings_chat_id;
-- -INDEXES

DROP TRIGGER IF EXISTS set_updated_at_chat_settings_trigger ON chat_settings;
DROP FUNCTION IF EXISTS set_updated_at_chat_settings();
DROP TABLE IF EXISTS chat_settings;
-- +goose StatementEnd
