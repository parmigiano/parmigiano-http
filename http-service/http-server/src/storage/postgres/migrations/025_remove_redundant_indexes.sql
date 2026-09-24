-- UNIQUE constraints already provide B-tree indexes for these columns.
DROP INDEX IF EXISTS idx_user_cores_email;
DROP INDEX IF EXISTS idx_user_cores_user_uid;
DROP INDEX IF EXISTS idx_user_profiles_user_uid;
DROP INDEX IF EXISTS idx_user_profiles_username;
DROP INDEX IF EXISTS idx_user_profile_accesses_user_uid;

-- uq_chat_settings_chat_id from migration 024 supersedes this single-column index.
DROP INDEX IF EXISTS idx_chat_settings_chat_id;
