-- +goose Up
-- +goose StatementBegin
CREATE TABLE IF NOT EXISTS message_edits (
    id SERIAL PRIMARY KEY,
    message_id BIGINT REFERENCES messages(id) ON DELETE CASCADE,
    old_content TEXT,
    new_content TEXT NOT NULL,
    editor_uid BIGINT REFERENCES user_cores(user_uid) ON DELETE SET NULL,
    edited_at TIMESTAMP NOT NULL DEFAULT NOW()
);
-- +goose StatementEnd

-- +goose Down
-- +goose StatementBegin
DROP TABLE IF EXISTS message_edits;
-- +goose StatementEnd
