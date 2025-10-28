package types

import "parmigiano/http/infra/store/postgres/models"

type AuthToken struct {
	User models.UserInfo `json:"user"`
}
