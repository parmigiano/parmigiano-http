-- +INDEXES
CREATE INDEX IF NOT EXISTS idx_message_statuses_message_id ON message_statuses(message_id);
CREATE INDEX IF NOT EXISTS idx_message_statuses_receiver_uid ON message_statuses(receiver_uid);
-- +INDEXES

-- DROP INDEX IF EXISTS idx_message_statuses_message_id;
-- DROP INDEX IF EXISTS idx_message_statuses_receiver_uid;