package store

import (
	"context"
	"database/sql"
	"errors"
	"parmigiano/http/infra/logger"
	"parmigiano/http/infra/store/postgres/models"
	"time"
)

type UserStore struct {
	db     *sql.DB
	logger *logger.Logger
}

func (s *UserStore) Get_UserInfoByAccessToken(ctx context.Context, token string) (*models.UserInfo, error) {
	user := models.UserInfo{}

	query := `
		SELECT
			user_cores.id,
			user_cores.created_at,
			user_cores.updated_at,
			user_cores.user_uuid,
			user_profiles.avatar,
			user_profiles.username,
			user_cores.email,
			user_cores.password,
			user_cores.access_token,
			user_cores.refresh_token
		FROM user_cores
		LEFT JOIN user_profiles ON user_cores.user_uuid = user_profiles.user_uuid
		WHERE user_cores.access_token = $1
		LIMIT 1
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	row := s.db.QueryRowContext(ctx, query, token)

	if err := row.Scan(
		&user.ID,
		&user.CreatedAt,
		&user.UpdatedAt,
		&user.UserUUID,
		&user.Avatar,
		&user.Username,
		&user.Email,
		&user.Password,
		&user.AccessToken,
		&user.RefreshToken,
	); err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return nil, nil
		}

		return nil, err
	}

	return &user, nil
}
