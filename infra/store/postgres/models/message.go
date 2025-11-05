package models

import "time"

type Message struct {
	ID          uint64     `json:"id" db:"id"`
	CreatedAt   time.Time  `json:"created_at" db:"created_at"`
	UpdatedAt   time.Time  `json:"updated_at" db:"updated_at"`
	DeletedAt   *time.Time `json:"deleted_at,omitempty" db:"deleted_at"`
	ChatID      uint64     `json:"chat_id" db:"chat_id"`
	SenderUid   uint64     `json:"sender_uid" db:"sender_uid"`
	Content     string     `json:"content" db:"content"`
	ContentType string     `json:"content_type" db:"content_type"`
	Attachments *string    `json:"attachments,omitempty" db:"attachments"`
	IsEdited    bool       `json:"is_edited" db:"is_edited"`
	IsDeleted   bool       `json:"is_deleted" db:"is_deleted"`
	IsPinned    bool       `json:"is_pinned" db:"is_pinned"`
}

type MessageStatus struct {
	ID          uint64     `json:"id" db:"id"`
	MessageId   uint64     `json:"message_id" db:"message_id"`
	ReceiverUid uint64     `json:"receiver_uid" db:"receiver_uid"`
	DeliveredAt time.Time  `json:"delivered_at" db:"delivered_at"`
	ReadAt      *time.Time `json:"read_at,omitempty" db:"read_at"`
}

type MessageEdit struct {
	ID         uint64    `json:"id" db:"id"`
	MessageId  uint64    `json:"message_id" db:"message_id"`
	OldContent *string   `json:"old_content,omitempty" db:"old_content"`
	NewContent *string   `json:"new_content" db:"new_content"`
	EditorUid  *uint64   `json:"editor_uid,omitempty" db:"editor_uid"`
	EditedAt   time.Time `json:"edited_at" db:"edited_at"`
}

type OnesMessage struct {
	ID          uint64     `json:"id" db:"id"` // message id
	ChatID      uint64     `json:"chat_id" db:"chat_id"`
	SenderUid   uint64     `json:"sender_uid" db:"sender_uid"`
	Content     string     `json:"content" db:"content"`
	ContentType string     `json:"content_type" db:"content_type"`
	IsEdited    bool       `json:"is_edited" db:"is_edited"`
	IsPinned    bool       `json:"is_pinned" db:"is_pinned"`
	DeliveredAt time.Time  `json:"delivered_at" db:"delivered_at"`
	ReadAt      *time.Time `json:"read_at,omitempty" db:"read_at"`
	EditContent *string    `json:"edit_content" db:"edit_content"` // NewContent in MessageEdit (SAVE AS EditContent)
}
