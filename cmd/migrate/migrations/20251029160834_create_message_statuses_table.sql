-- +goose Up
-- +goose StatementBegin
CREATE TABLE IF NOT EXISTS message_statuses (
    id SERIAL PRIMARY KEY,
    message_id BIGINT REFERENCES messages(id) ON DELETE CASCADE,
    receiver_uid BIGINT NOT NULL REFERENCES user_cores(user_uid) ON DELETE CASCADE,
    delivered_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW(),
    read_at TIMESTAMP WITH TIME ZONE -- Если есть запись, тогда клиент прочитал сообщение
);
-- +goose StatementEnd

-- +goose Down
-- +goose StatementBegin
DROP TABLE IF EXISTS message_statuses;
-- +goose StatementEnd
