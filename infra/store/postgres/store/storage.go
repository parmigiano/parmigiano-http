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
		Create_UserProfileAccess(tx *sql.Tx, ctx context.Context, user *models.UserProfileAccess) error
		Create_UserActive(tx *sql.Tx, ctx context.Context, user *models.UserActive) error

		Get_UserInfoByUserUid(ctx context.Context, userUid uint64) (*models.UserInfo, error)
		Get_UserCoreByUid(ctx context.Context, userUid uint64) (*models.UserCore, error)
		Get_UserProfileByUid(ctx context.Context, userUid uint64) (*models.UserProfile, error)
		Get_UserCoreByEmail(ctx context.Context, email string) (*models.UserCore, error)

		Update_UserEmailConfirmedByUid(ctx context.Context, userUid uint64, confirmed bool) error
		Update_UserEmailByUid(ctx context.Context, userUid uint64, email string) error
		Update_UserCoreByUid(ctx context.Context, tx *sql.Tx, user *models.UserProfileUpd) error
		Update_UserProfileByUid(ctx context.Context, tx *sql.Tx, user *models.UserProfileUpd) error
		Update_UserProfileAccessByUid(ctx context.Context, tx *sql.Tx, user *models.UserProfileUpd) error
		Update_UserAvatarByUid(ctx context.Context, userUid uint64, avatar string) error

		Delete_UserByUid(ctx context.Context, userUid uint64) error
		Delete_UserIfEmailNotConfirmed(ctx context.Context) error
	}
	Messages interface { //nolint
		Get_MessagesHistoryByReceiver(ctx context.Context, receiverUid, senderUid uint64) (*[]models.OnesMessage, error)
	}
	Chats interface { //nolint
		Create_Chat(ctx context.Context, chat *models.Chat) (uint64, error)
		Create_ChatMember(ctx context.Context, member *models.ChatMember) error

		Get_ChatPrivateByUser(ctx context.Context, myUserUid, otherUserUid uint64) (*models.Chat, error)
		Get_ChatsMyHistory(ctx context.Context, userUid uint64) (*[]models.ChatMinimalWithLMessage, error)
		Get_ChatsBySearchUsername(ctx context.Context, myUserUid uint64, username string) (*[]models.ChatMinimalWithLMessage, error)
	}
}

func NewStorage(db *sql.DB, logger *logger.Logger) Storage {
	return Storage{
		Users:    &UserStore{db, logger},
		Messages: &MessageStore{db, logger},
		Chats:    &ChatStore{db, logger},
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
