-- +goose Up
-- +goose StatementBegin
CREATE TABLE IF NOT EXISTS message_edits (
    id SERIAL PRIMARY KEY,
    message_id BIGINT REFERENCES messages(id) ON DELETE CASCADE,
    old_content TEXT,
    new_content TEXT,
    editor_uid BIGINT REFERENCES user_cores(user_uid) ON DELETE SET NULL,
    edited_at TIMESTAMPTZ NOT NULL DEFAULT (timezone('UTC', now()))
);

-- +INDEXES
CREATE INDEX IF NOT EXISTS idx_message_edits_message_id ON message_edits(message_id);
-- +INDEXES
-- +goose StatementEnd

-- +goose Down
-- +goose StatementBegin
-- -INDEXES
DROP INDEX IF EXISTS idx_message_edits_message_id;
-- -INDEXES

DROP TABLE IF EXISTS message_edits;
-- +goose StatementEnd
