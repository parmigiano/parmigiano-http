-- +INDEXES
CREATE INDEX IF NOT EXISTS idx_messages_chat_id ON messages(chat_id);
CREATE INDEX IF NOT EXISTS idx_messages_chat_id_id ON messages(chat_id, id DESC);
CREATE INDEX IF NOT EXISTS idx_messages_sender_uid ON messages(sender_uid);
-- +INDEXES

-- DROP INDEX IF EXISTS idx_messages_chat_id;
-- DROP INDEX IF EXISTS idx_messages_chat_id_id;
-- DROP INDEX IF EXISTS idx_messages_sender_uid;