-- +INDEXES
CREATE INDEX IF NOT EXISTS idx_user_cores_email ON user_cores(email);
CREATE INDEX IF NOT EXISTS idx_user_cores_user_uid ON user_cores(user_uid);
-- +INDEXES

-- DROP INDEX IF EXISTS idx_user_cores_email;
-- DROP INDEX IF EXISTS idx_user_cores_user_uid;