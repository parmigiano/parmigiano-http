-- +INDEXES
CREATE INDEX IF NOT EXISTS idx_message_edits_message_id ON message_edits(message_id);
-- +INDEXES

-- DROP INDEX IF EXISTS idx_message_edits_message_id;