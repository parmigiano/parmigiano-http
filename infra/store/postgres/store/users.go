package store

import (
	"context"
	"database/sql"
	"errors"
	"parmigiano/http/infra/logger"
	"parmigiano/http/infra/store/postgres/models"
	"parmigiano/http/pkg/httpx/httperr"
	"strings"
	"time"
)

type UserStore struct {
	db     *sql.DB
	logger *logger.Logger
}

func (s *UserStore) Create_UserCore(tx *sql.Tx, ctx context.Context, user *models.UserCore) error {
	query := `
		INSERT INTO user_cores (user_uuid, email, password, access_token) VALUES ($1, $2, $3, $4)
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	_, err := tx.ExecContext(ctx, query, user.UserUUID, user.Email, user.Password, user.AccessToken)
	if err != nil {
		if strings.Contains(err.Error(), `user_cores_email_key`) {
			return httperr.Err_DuplicateEmail
		}

		return err
	}

	return nil
}

func (s *UserStore) Create_UserProfile(tx *sql.Tx, ctx context.Context, user *models.UserProfile) error {
	query := `
		INSERT INTO user_profiles (user_uuid, avatar, username) VALUES ($1, $2, $3)
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	_, err := tx.ExecContext(ctx, query, user.UserUUID, user.Avatar, user.Username)
	if err != nil {
		if strings.Contains(err.Error(), `user_cores_email_key`) {
			return httperr.Err_DuplicateEmail
		}

		return err
	}

	return nil
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

func (s *UserStore) Get_UserCoreByUuid(ctx context.Context, uuid string) (*models.UserCore, error) {
	user := models.UserCore{}

	query := `
		SELECT
		    id,
		    user_uuid,
		    email,
			password,
		    access_token,
		    refresh_token
		FROM user_cores
		WHERE user_uuid = $1
		LIMIT 1
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	row := s.db.QueryRowContext(ctx, query, uuid)

	if err := row.Scan(&user.ID, &user.UserUUID, &user.Email, &user.Password, &user.AccessToken, &user.RefreshToken); err != nil {
		if err == sql.ErrNoRows {
			return nil, nil
		}

		return nil, err
	}

	return &user, nil
}

func (s *UserStore) Get_UserProfileByUuid(ctx context.Context, uuid string) (*models.UserProfile, error) {
	user := models.UserProfile{}

	query := `
		SELECT
		    id,
		    user_uuid,
		    avatar,
			username
		FROM user_profiles
		WHERE user_uuid = $1
		LIMIT 1
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	row := s.db.QueryRowContext(ctx, query, uuid)

	if err := row.Scan(&user.ID, &user.UserUUID, &user.Avatar, &user.Username); err != nil {
		if err == sql.ErrNoRows {
			return nil, nil
		}

		return nil, err
	}

	return &user, nil
}

func (s *UserStore) Get_UserCoreByEmail(ctx context.Context, email string) (*models.UserCore, error) {
	user := models.UserCore{}

	query := `
		SELECT
		    id,
		    user_uuid,
		    email,
			password,
		    access_token,
		    refresh_token
		FROM user_cores
		WHERE email = $1
		LIMIT 1
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	row := s.db.QueryRowContext(ctx, query, email)

	if err := row.Scan(&user.ID, &user.UserUUID, &user.Email, &user.Password, &user.AccessToken, &user.RefreshToken); err != nil {
		if err == sql.ErrNoRows {
			return nil, nil
		}

		return nil, err
	}

	return &user, nil
}

func (s *UserStore) Delete_UserByUuid(ctx context.Context, uuid string) error {
	query := `
		DELETE FROM user_cores WHERE user_uuid = $1
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	del, err := s.db.ExecContext(ctx, query, uuid)
	if err != nil {
		return err
	}

	delRows, _ := del.RowsAffected()
	if delRows == 0 {
		return httperr.Err_NotDeleted
	}

	return nil
}
