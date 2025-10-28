package models

import "time"

type UserProfile struct {
	ID        uint64    `json:"id" db:"id"`
	CreatedAt time.Time `json:"created_at" db:"created_at"`
	UpdatedAt time.Time `json:"updated_at" db:"updated_at"`
	UserUUID  string    `json:"user_uuid" db:"user_uuid"`
	Avatar    []byte    `json:"avatar" db:"avatar"`
	Username  string    `json:"username" db:"username"`
}

type UserCore struct {
	ID           uint64    `json:"id" db:"id"`
	CreatedAt    time.Time `json:"created_at" db:"created_at"`
	UpdatedAt    time.Time `json:"updated_at" db:"updated_at"`
	UserUUID     string    `json:"user_uuid" db:"user_uuid"`
	Email        string    `json:"email" db:"email"`
	Password     string    `json:"password" db:"password"`
	AccessToken  string    `json:"access_token" db:"access_token"`
	RefreshToken *string   `json:"refresh_token" db:"refresh_token"`
}

type UserInfo struct {
	ID           uint64    `json:"id" db:"id"`
	CreatedAt    time.Time `json:"created_at" db:"created_at"`
	UpdatedAt    time.Time `json:"updated_at" db:"updated_at"`
	UserUUID     string    `json:"user_uuid" db:"user_uuid"`
	Avatar       []byte    `json:"avatar" db:"avatar"`
	Username     string    `json:"username" db:"username"`
	Email        string    `json:"email" db:"email"`
	Password     string    `json:"password" db:"password"`
	AccessToken  string    `json:"access_token" db:"access_token"`
	RefreshToken *string   `json:"refresh_token" db:"refresh_token"`
}
