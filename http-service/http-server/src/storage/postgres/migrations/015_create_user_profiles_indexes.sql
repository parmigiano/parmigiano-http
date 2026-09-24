-- +INDEXES
CREATE INDEX IF NOT EXISTS idx_user_profiles_user_uid ON user_profiles(user_uid);
CREATE INDEX IF NOT EXISTS idx_user_profiles_username ON user_profiles(username);
-- +INDEXES

-- DROP INDEX IF EXISTS idx_user_profiles_user_uid;
-- DROP INDEX IF EXISTS idx_user_profiles_username;