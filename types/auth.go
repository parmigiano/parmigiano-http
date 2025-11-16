package types

import (
	"parmigiano/http/infra/store/postgres/models"
	"time"
)

type AuthToken struct {
	User models.UserInfo `json:"user"`
}

type ReqAuthToken struct {
	UID       uint64    `json:"uid"`
	Timestamp time.Time `json:"timestamp"`
}
