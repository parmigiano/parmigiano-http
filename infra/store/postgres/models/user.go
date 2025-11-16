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
	Overview  *string   `json:"overview" db:"overview"`
	Phone     *string   `json:"phone" db:"phone"`
}

type UserProfileAccess struct {
	ID              uint64    `json:"id" db:"id"`
	CreatedAt       time.Time `json:"created_at" db:"created_at"`
	UpdatedAt       time.Time `json:"updated_at" db:"updated_at"`
	UserUid         uint64    `json:"user_uid" db:"user_uid"`
	EmailVisible    bool      `json:"email_visible" db:"email_visible"`
	UsernameVisible bool      `json:"username_visible" db:"username_visible"`
	PhoneVisible    bool      `json:"phone_visible" db:"phone_visible"`
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
}

// dop. models
type UserProfileUpd struct {
	UserUid         uint64  `json:"user_uid" db:"user_uid"`
	Username        *string `json:"username" db:"username"`
	Name            *string `json:"name" db:"name"`
	Email           *string `json:"email" db:"email"`
	Phone           *string `json:"phone" db:"phone"`
	Overview        *string `json:"overview" db:"overview"`
	UsernameVisible *bool   `json:"username_visible" db:"username_visible"`
	PhoneVisible    *bool   `json:"phone_visible" db:"phone_visible"`
	EmailVisible    *bool   `json:"email_visible" db:"email_visible"`
	Password        *string `json:"password" db:"password"`
}

type UserInfo struct {
	ID              uint64    `json:"id" db:"id"`
	CreatedAt       time.Time `json:"created_at" db:"created_at"`
	UserUid         uint64    `json:"user_uid" db:"user_uid"`
	Online          bool      `json:"online" db:"online"`
	LastOnlineDate  time.Time `json:"last_online_date" db:"last_online_date"`
	Avatar          *string   `json:"avatar" db:"avatar"`
	Name            string    `json:"name" db:"name"`
	Username        string    `json:"username" db:"username"`
	UsernameVisible bool      `json:"username_visible" db:"username_visible"`
	Email           string    `json:"email" db:"email"`
	EmailVisible    bool      `json:"email_visible" db:"email_visible"`
	EmailConfirmed  bool      `json:"email_confirmed" db:"email_confirmed"`
	Phone           *string   `json:"phone" db:"phone"`
	PhoneVisible    bool      `json:"phone_visible" db:"phone_visible"`
	Overview        *string   `json:"overview" db:"overview"`
	AccessToken     string    `json:"access_token" db:"access_token"`
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
