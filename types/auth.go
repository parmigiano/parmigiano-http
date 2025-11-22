package types

import (
	"parmigiano/http/infra/store/postgres/models"
)

type Session struct {
	UserUid uint64 `json:"user_uid"`
}

type AuthToken struct {
	User models.UserInfo `json:"user"`
}
