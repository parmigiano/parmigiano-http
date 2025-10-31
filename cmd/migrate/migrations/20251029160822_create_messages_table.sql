-- +goose Up
-- +goose StatementBegin
CREATE TABLE IF NOT EXISTS messages (
    id SERIAL PRIMARY KEY,
    -- Временные метки
    created_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMP WITH TIME ZONE NOT NULL DEFAULT NOW(),
    deleted_at TIMESTAMP WITH TIME ZONE,
    -- Пользователи
    sender_uid BIGINT NOT NULL REFERENCES user_cores(user_uid) ON DELETE CASCADE, -- Получатель
    receiver_uid BIGINT NOT NULL REFERENCES user_cores(user_uid) ON DELETE CASCADE, -- Отправитель
    -- Контент
    content TEXT NOT NULL,
    content_type VARCHAR(32) CHECK (content_type IN ('text', 'image', 'video', 'file', 'voice')) DEFAULT 'text',
    attachments JSONB,
    -- Доп. поля
    is_edited BOOLEAN DEFAULT FALSE,
    is_deleted BOOLEAN DEFAULT FALSE,
    is_pinned BOOLEAN DEFAULT FALSE
);

CREATE OR REPLACE FUNCTION set_updated_at_messages()
RETURNS TRIGGER AS $$
BEGIN
    NEW.updated_at = NOW();
    RETURN NEW;
END;
$$ LANGUAGE plpgsql;

CREATE TRIGGER set_updated_at_messages_trigger
    BEFORE UPDATE ON messages
    FOR EACH ROW
    EXECUTE FUNCTION set_updated_at_messages();
-- +goose StatementEnd

-- +goose Down
-- +goose StatementBegin
DROP TRIGGER IF EXISTS set_updated_at_messages_trigger ON messages;
DROP FUNCTION IF EXISTS set_updated_at_messages();
DROP TABLE IF EXISTS messages;
-- +goose StatementEnd
