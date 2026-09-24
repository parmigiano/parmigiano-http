-- +INDEXES
CREATE INDEX IF NOT EXISTS idx_user_profile_accesses_user_uid ON user_profile_accesses(user_uid);
-- +INDEXES

-- DROP INDEX IF EXISTS idx_user_profile_accesses_user_uid;