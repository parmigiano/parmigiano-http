-- +INDEXES
CREATE INDEX IF NOT EXISTS idx_chat_settings_chat_id ON chat_settings(chat_id);
-- +INDEXES

-- -INDEXES
-- DROP INDEX IF EXISTS idx_chat_settings_chat_id;
-- -INDEXES