package store

import (
	"context"
	"database/sql"
	"parmigiano/http/infra/logger"
	"parmigiano/http/infra/store/postgres/models"
)

type Storage struct {
	Users interface { //nolint
		Create_UserCore(tx *sql.Tx, ctx context.Context, user *models.UserCore) error
		Create_UserProfile(tx *sql.Tx, ctx context.Context, user *models.UserProfile) error

		Get_UserInfoByAccessToken(ctx context.Context, token string) (*models.UserInfo, error)
		Get_UserCoreByUuid(ctx context.Context, uuid string) (*models.UserCore, error)
		Get_UserProfileByUuid(ctx context.Context, uuid string) (*models.UserProfile, error)
		Get_UserCoreByEmail(ctx context.Context, email string) (*models.UserCore, error)

		Delete_UserByUuid(ctx context.Context, uuid string) error
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
