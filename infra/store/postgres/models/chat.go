package models

import "time"

type Chat struct {
	ID        uint64    `json:"id" db:"id"`
	CreatedAt time.Time `json:"created_at" db:"created_at"`
	UpdatedAt time.Time `json:"updated_at" db:"updated_at"`
	ChatType  string    `json:"chat_type" db:"chat_type"` // "private", "group", "channel"
	Title     *string   `json:"title,omitempty" db:"title"`
}

type ChatMember struct {
	ID        uint64    `json:"id" db:"id"`
	CreatedAt time.Time `json:"created_at" db:"created_at"`
	ChatID    uint64    `json:"chat_id" db:"chat_id"`
	UserUid   uint64    `json:"user_uid" db:"user_uid"`
	Role      string    `json:"role" db:"role"` // "owner", "admin", "member"
}

type ChatSetting struct {
	ID               uint64    `json:"id" db:"id"`
	CreatedAt        time.Time `json:"created_at" db:"created_at"`
	UpdatedAt        time.Time `json:"updated_at" db:"updated_at"`
	ChatID           uint64    `json:"chat_id" db:"chat_id"`
	CustomBackground *string   `json:"custom_background" db:"custom_background"`
	Blocked          bool      `json:"blocked" db:"blocked"`
}

type ChatMinimalWithLMessage struct {
	ID                 uint64     `json:"id" db:"id"`
	Name               string     `json:"name" db:"name"`
	Username           string     `json:"username" db:"username"`
	Avatar             *string    `json:"avatar" db:"avatar"`
	UserUid            uint64     `json:"user_uid" db:"user_uid"`
	Email              string     `json:"email" db:"email"`
	Online             bool       `json:"online" db:"online"`
	LastOnlineDate     time.Time  `json:"last_online_date" db:"last_online_date"`
	LastMessage        *string    `json:"last_message" db:"last_message"`
	LastMessageDate    *time.Time `json:"last_message_date" db:"last_message_date"`
	UnreadMessageCount uint16     `json:"unread_message_count" db:"unread_message_count"`
}
