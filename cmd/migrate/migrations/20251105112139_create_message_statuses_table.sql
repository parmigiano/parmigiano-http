-- +goose Up
-- +goose StatementBegin
CREATE TABLE IF NOT EXISTS message_statuses (
    id SERIAL PRIMARY KEY,
    message_id BIGINT REFERENCES messages(id) ON DELETE CASCADE,
    receiver_uid BIGINT NOT NULL REFERENCES user_cores(user_uid) ON DELETE CASCADE,
    delivered_at TIMESTAMPTZ NOT NULL DEFAULT (timezone('UTC', now())),
    read_at TIMESTAMPTZ -- Если есть запись, тогда клиент прочитал сообщение
);

-- +INDEXES
CREATE INDEX IF NOT EXISTS idx_message_statuses_message_id ON message_statuses(message_id);
CREATE INDEX IF NOT EXISTS idx_message_statuses_receiver_uid ON message_statuses(receiver_uid);
-- +INDEXES
-- +goose StatementEnd

-- +goose Down
-- +goose StatementBegin
-- -INDEXES
DROP INDEX IF EXISTS idx_message_statuses_message_id;
DROP INDEX IF EXISTS idx_message_statuses_receiver_uid;
-- -INDEXES

DROP TABLE IF EXISTS message_statuses;
-- +goose StatementEnd
