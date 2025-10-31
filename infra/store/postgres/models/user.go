package models

import "time"

type UserProfile struct {
	ID        uint64    `json:"id" db:"id"`
	CreatedAt time.Time `json:"created_at" db:"created_at"`
	UpdatedAt time.Time `json:"updated_at" db:"updated_at"`
	UserUid   uint64    `json:"user_uid" db:"user_uid"`
	Avatar    *string   `json:"avatar" db:"avatar"`
	Username  string    `json:"username" db:"username"`
}

type UserCore struct {
	ID           uint64    `json:"id" db:"id"`
	CreatedAt    time.Time `json:"created_at" db:"created_at"`
	UpdatedAt    time.Time `json:"updated_at" db:"updated_at"`
	UserUid      uint64    `json:"user_uid" db:"user_uid"`
	Email        string    `json:"email" db:"email"`
	Password     string    `json:"password" db:"password"`
	AccessToken  string    `json:"access_token" db:"access_token"`
	RefreshToken *string   `json:"refresh_token" db:"refresh_token"`
}

type UserInfo struct {
	ID           uint64    `json:"id" db:"id"`
	CreatedAt    time.Time `json:"created_at" db:"created_at"`
	UpdatedAt    time.Time `json:"updated_at" db:"updated_at"`
	UserUid      uint64    `json:"user_uid" db:"user_uid"`
	Avatar       *string   `json:"avatar" db:"avatar"`
	Username     string    `json:"username" db:"username"`
	Email        string    `json:"email" db:"email"`
	Password     string    `json:"password" db:"password"`
	AccessToken  string    `json:"access_token" db:"access_token"`
	RefreshToken *string   `json:"refresh_token" db:"refresh_token"`
}

type UserMinimalWithLMessage struct {
	ID              uint64     `json:"id" db:"id"`
	Username        string     `json:"username" db:"username"`
	Avatar          *string    `json:"avatar" db:"avatar"`
	UserUid         uint64     `json:"user_uid" db:"user_uid"`
	LastMessage     *string    `json:"last_message" db:"last_message"`
	LastMessageDate *time.Time `json:"last_message_date" db:"last_message_date"`
}

type UserMinimal struct {
	ID        uint64    `json:"id" db:"id"`
	CreatedAt time.Time `json:"created_at" db:"created_at"`
	UpdatedAt time.Time `json:"updated_at" db:"updated_at"`
	UserUid   uint64    `json:"user_uid" db:"user_uid"`
	Avatar    *string   `json:"avatar" db:"avatar"`
	Username  string    `json:"username" db:"username"`
	Email     string    `json:"email" db:"email"`
}
