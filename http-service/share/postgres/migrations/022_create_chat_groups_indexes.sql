-- +INDEXES
CREATE INDEX IF NOT EXISTS idx_chat_groups_user_uid ON chat_groups(user_uid);
-- +INDEXES

-- DROP INDEX IF EXISTS idx_chat_groups_user_uid;