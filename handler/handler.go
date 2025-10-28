package handler

import (
	"database/sql"
	"parmigiano/http/infra/logger"
	"parmigiano/http/infra/store/postgres/store"
)

type BaseHandler struct {
	Db     *sql.DB
	Logger *logger.Logger
	Store  store.Storage
}
