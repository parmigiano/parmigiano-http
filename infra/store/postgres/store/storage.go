package store

import (
	"context"
	"database/sql"
	"parmigiano/http/infra/logger"
	"parmigiano/http/infra/store/postgres/models"
)

type Storage struct {
	Users interface { //nolint
		Get_UserInfoByAccessToken(ctx context.Context, token string) (*models.UserInfo, error)
	}
}

func NewStorage(db *sql.DB, logger *logger.Logger) Storage {
	return Storage{
		Users: &UserStore{db, logger},
	}
}

func WithTx(db *sql.DB, ctx context.Context, fn func(*sql.Tx) error) error {
	tx, err := db.BeginTx(ctx, nil)
	if err != nil {
		return err
	}

	if err := fn(tx); err != nil {
		_ = tx.Rollback()
		return err
	}

	return tx.Commit()
}
