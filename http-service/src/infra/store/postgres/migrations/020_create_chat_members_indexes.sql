-- +INDEXES
CREATE INDEX IF NOT EXISTS idx_chat_members_chat_id ON chat_members(chat_id);
CREATE INDEX IF NOT EXISTS idx_chat_members_user_uid ON chat_members(user_uid);
-- +INDEXES

-- DROP INDEX IF EXISTS idx_chat_members_chat_id;
-- DROP INDEX IF EXISTS idx_chat_members_user_uid;
