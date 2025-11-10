package models

import "time"

// database model
type UserProfile struct {
	ID        uint64    `json:"id" db:"id"`
	CreatedAt time.Time `json:"created_at" db:"created_at"`
	UpdatedAt time.Time `json:"updated_at" db:"updated_at"`
	UserUid   uint64    `json:"user_uid" db:"user_uid"`
	Avatar    *string   `json:"avatar" db:"avatar"`
	Name      string    `json:"name" db:"name"`
	Username  string    `json:"username" db:"username"`
}

type UserActive struct {
	ID        uint64    `json:"id" db:"id"`
	CreatedAt time.Time `json:"created_at" db:"created_at"`
	UpdatedAt time.Time `json:"updated_at" db:"updated_at"`
	UserUid   uint64    `json:"user_uid" db:"user_uid"`
	Online    bool      `json:"online" db:"online"`
}

type UserCore struct {
	ID             uint64    `json:"id" db:"id"`
	CreatedAt      time.Time `json:"created_at" db:"created_at"`
	UpdatedAt      time.Time `json:"updated_at" db:"updated_at"`
	UserUid        uint64    `json:"user_uid" db:"user_uid"`
	Email          string    `json:"email" db:"email"`
	EmailConfirmed bool      `json:"email_confirmed" db:"email_confirmed"`
	Password       string    `json:"password" db:"password"`
	AccessToken    string    `json:"access_token" db:"access_token"`
	RefreshToken   *string   `json:"refresh_token" db:"refresh_token"`
}

// dop. models
type UserProfileUpd struct {
	UserUid  uint64  `json:"user_uid" db:"user_uid"`
	Username *string `json:"username" db:"username"`
	Name     *string `json:"name" db:"name"`
	Email    *string `json:"email" db:"email"`
	Password *string `json:"password" db:"password"`
}

type UserInfo struct {
	ID             uint64    `json:"id" db:"id"`
	CreatedAt      time.Time `json:"created_at" db:"created_at"`
	UpdatedAt      time.Time `json:"updated_at" db:"updated_at"`
	UserUid        uint64    `json:"user_uid" db:"user_uid"`
	Avatar         *string   `json:"avatar" db:"avatar"`
	Name           string    `json:"name" db:"name"`
	Username       string    `json:"username" db:"username"`
	Email          string    `json:"email" db:"email"`
	EmailConfirmed bool      `json:"email_confirmed" db:"email_confirmed"`
	AccessToken    string    `json:"access_token" db:"access_token"`
	RefreshToken   *string   `json:"refresh_token" db:"refresh_token"`
}

type UserMinimalWithLMessage struct {
	ID                 uint64     `json:"id" db:"id"`
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

type UserMinimal struct {
	ID             uint64    `json:"id" db:"id"`
	CreatedAt      time.Time `json:"created_at" db:"created_at"`
	UpdatedAt      time.Time `json:"updated_at" db:"updated_at"`
	UserUid        uint64    `json:"user_uid" db:"user_uid"`
	Avatar         *string   `json:"avatar" db:"avatar"`
	Username       string    `json:"username" db:"username"`
	Email          string    `json:"email" db:"email"`
	EmailConfirmed bool      `json:"email_confirmed" db:"email_confirmed"`
}
