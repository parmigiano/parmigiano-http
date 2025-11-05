-- +goose Up
-- +goose StatementBegin
CREATE TABLE IF NOT EXISTS chat_members (
    id BIGSERIAL PRIMARY KEY,
    created_at TIMESTAMPTZ NOT NULL DEFAULT (timezone('UTC', now())),
    chat_id BIGINT REFERENCES chats(id) ON DELETE CASCADE,
    user_uid BIGINT REFERENCES user_cores(user_uid) ON DELETE CASCADE,
    role VARCHAR(16) DEFAULT 'member' CHECK (role IN ('owner', 'admin', 'member')),

    UNIQUE (chat_id, user_uid)
);
-- +goose StatementEnd

-- +goose Down
-- +goose StatementBegin
DROP TABLE IF EXISTS chat_members;
-- +goose StatementEnd
