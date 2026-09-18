-- Keep database limits aligned with HTTP validation.
ALTER TABLE user_cores
    ALTER COLUMN email TYPE VARCHAR(254);

ALTER TABLE user_profiles
    ALTER COLUMN name TYPE VARCHAR(96);

-- Restore missing referential integrity.
ALTER TABLE user_public_keys
    ADD CONSTRAINT fk_user_public_keys_user
    FOREIGN KEY (user_uid) REFERENCES user_cores(user_uid) ON DELETE CASCADE;

ALTER TABLE chat_groups
    ADD CONSTRAINT fk_chat_groups_user
    FOREIGN KEY (user_uid) REFERENCES user_cores(user_uid) ON DELETE CASCADE;

-- Required relations must never be NULL.
ALTER TABLE chat_members
    ALTER COLUMN chat_id SET NOT NULL,
    ALTER COLUMN user_uid SET NOT NULL;

ALTER TABLE message_statuses
    ALTER COLUMN message_id SET NOT NULL;

ALTER TABLE message_edits
    ALTER COLUMN message_id SET NOT NULL;

ALTER TABLE chat_settings
    ALTER COLUMN chat_id SET NOT NULL;

-- There is exactly one settings row for a chat.
ALTER TABLE chat_settings
    ADD CONSTRAINT uq_chat_settings_chat_id UNIQUE (chat_id);

-- 0 is not a valid user reference. NULL means nobody/system.
UPDATE chat_settings
SET who_blocked_uid = NULL
WHERE who_blocked_uid = 0;

ALTER TABLE chat_settings
    ALTER COLUMN who_blocked_uid DROP DEFAULT,
    ALTER COLUMN who_blocked_uid DROP NOT NULL;

ALTER TABLE chat_settings
    ADD CONSTRAINT fk_chat_settings_blocked_by
    FOREIGN KEY (who_blocked_uid) REFERENCES user_cores(user_uid) ON DELETE SET NULL;

-- Normalize nullable flags/content left by the original schema.
UPDATE messages SET content_type = 'text' WHERE content_type IS NULL;
UPDATE messages SET is_edited = FALSE WHERE is_edited IS NULL;
UPDATE messages SET is_deleted = FALSE WHERE is_deleted IS NULL;
UPDATE messages SET is_pinned = FALSE WHERE is_pinned IS NULL;

ALTER TABLE messages
    ALTER COLUMN content_type SET NOT NULL,
    ALTER COLUMN is_edited SET NOT NULL,
    ALTER COLUMN is_deleted SET NOT NULL,
    ALTER COLUMN is_pinned SET NOT NULL;

-- TIMESTAMPTZ already stores an absolute instant; now() is the correct default.
ALTER TABLE user_cores ALTER COLUMN created_at SET DEFAULT now();
ALTER TABLE user_cores ALTER COLUMN updated_at SET DEFAULT now();
ALTER TABLE user_profiles ALTER COLUMN created_at SET DEFAULT now();
ALTER TABLE user_profiles ALTER COLUMN updated_at SET DEFAULT now();
ALTER TABLE user_public_keys ALTER COLUMN created_at SET DEFAULT now();
ALTER TABLE user_public_keys ALTER COLUMN updated_at SET DEFAULT now();
ALTER TABLE user_actives ALTER COLUMN created_at SET DEFAULT now();
ALTER TABLE user_actives ALTER COLUMN updated_at SET DEFAULT now();
ALTER TABLE chats ALTER COLUMN created_at SET DEFAULT now();
ALTER TABLE chats ALTER COLUMN updated_at SET DEFAULT now();
ALTER TABLE messages ALTER COLUMN created_at SET DEFAULT now();
ALTER TABLE messages ALTER COLUMN updated_at SET DEFAULT now();
ALTER TABLE message_statuses ALTER COLUMN delivered_at SET DEFAULT now();
ALTER TABLE message_edits ALTER COLUMN edited_at SET DEFAULT now();
ALTER TABLE user_profile_accesses ALTER COLUMN created_at SET DEFAULT now();
ALTER TABLE user_profile_accesses ALTER COLUMN updated_at SET DEFAULT now();
ALTER TABLE chat_settings ALTER COLUMN created_at SET DEFAULT now();
ALTER TABLE chat_settings ALTER COLUMN updated_at SET DEFAULT now();
ALTER TABLE chat_groups ALTER COLUMN created_at SET DEFAULT now();
ALTER TABLE chat_groups ALTER COLUMN updated_at SET DEFAULT now();
