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
		Create_UserActive(tx *sql.Tx, ctx context.Context, user *models.UserActive) error

		Get_UsersWithLMessage(ctx context.Context, userUid uint64) (*[]models.UserMinimalWithLMessage, error)
		Get_UserWithLMessage(ctx context.Context, userUid uint64) (*models.UserMinimalWithLMessage, error)
		Get_UserInfoByAccessToken(ctx context.Context, token string) (*models.UserInfo, error)
		Get_UserCoreByUid(ctx context.Context, userUid uint64) (*models.UserCore, error)
		Get_UserProfileByUid(ctx context.Context, userUid uint64) (*models.UserProfile, error)
		Get_UserCoreByEmail(ctx context.Context, email string) (*models.UserCore, error)

		Update_UserAvatarByUid(ctx context.Context, userUid uint64, avatar string) error

		Delete_UserByUid(ctx context.Context, userUid uint64) error
	}
	Messages interface { //nolint
		Get_MessagesHistoryByReceiver(ctx context.Context, receiverUid, senderUid uint64) (*[]models.OnesMessage, error)
	}
}

func NewStorage(db *sql.DB, logger *logger.Logger) Storage {
	return Storage{
		Users:    &UserStore{db, logger},
		Messages: &MessageStore{db, logger},
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
