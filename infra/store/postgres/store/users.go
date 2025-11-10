package store

import (
	"context"
	"database/sql"
	"errors"
	"fmt"
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
		INSERT INTO user_cores (user_uid, email, password, access_token) VALUES ($1, $2, $3, $4)
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	_, err := tx.ExecContext(ctx, query, user.UserUid, user.Email, user.Password, user.AccessToken)
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
		INSERT INTO user_profiles (user_uid, avatar, name, username) VALUES ($1, $2, $3, $4)
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	_, err := tx.ExecContext(ctx, query, user.UserUid, user.Avatar, user.Name, user.Username)
	if err != nil {
		if strings.Contains(err.Error(), `user_cores_email_key`) {
			return httperr.Err_DuplicateEmail
		}

		return err
	}

	return nil
}

func (s *UserStore) Create_UserActive(tx *sql.Tx, ctx context.Context, user *models.UserActive) error {
	query := `
		INSERT INTO user_actives (user_uid, online) VALUES ($1, $2)
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	_, err := tx.ExecContext(ctx, query, user.UserUid, false)
	if err != nil {
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
			user_cores.user_uid,
			user_profiles.avatar,
			user_profiles.name,
			user_profiles.username,
			user_cores.email,
			user_cores.email_confirmed,
			user_cores.access_token,
			user_cores.refresh_token
		FROM user_cores
		LEFT JOIN user_profiles ON user_cores.user_uid = user_profiles.user_uid
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
		&user.UserUid,
		&user.Avatar,
		&user.Name,
		&user.Username,
		&user.Email,
		&user.EmailConfirmed,
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

func (s *UserStore) Get_UserCoreByUid(ctx context.Context, userUid uint64) (*models.UserCore, error) {
	user := models.UserCore{}

	query := `
		SELECT
		    id,
		    user_uid,
		    email,
			email_confirmed,
			password,
		    access_token,
		    refresh_token
		FROM user_cores
		WHERE user_uid = $1
		LIMIT 1
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	row := s.db.QueryRowContext(ctx, query, userUid)

	if err := row.Scan(
		&user.ID,
		&user.UserUid,
		&user.Email,
		&user.EmailConfirmed,
		&user.Password,
		&user.AccessToken,
		&user.RefreshToken,
	); err != nil {
		if err == sql.ErrNoRows {
			return nil, nil
		}

		return nil, err
	}

	return &user, nil
}

func (s *UserStore) Get_UserProfileByUid(ctx context.Context, userUid uint64) (*models.UserProfile, error) {
	user := models.UserProfile{}

	query := `
		SELECT
		    id,
		    user_uid,
		    avatar,
			username
		FROM user_profiles
		WHERE user_uid = $1
		LIMIT 1
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	row := s.db.QueryRowContext(ctx, query, userUid)

	if err := row.Scan(&user.ID, &user.UserUid, &user.Avatar, &user.Username); err != nil {
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
		    user_uid,
		    email,
			email_confirmed,
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

	if err := row.Scan(
		&user.ID,
		&user.UserUid,
		&user.Email,
		&user.EmailConfirmed,
		&user.Password,
		&user.AccessToken,
		&user.RefreshToken,
	); err != nil {
		if err == sql.ErrNoRows {
			return nil, nil
		}

		return nil, err
	}

	return &user, nil
}

func (s *UserStore) Update_UserEmailConfirmedByUid(ctx context.Context, userUid uint64, confirmed bool) error {
	tx, err := s.db.BeginTx(ctx, nil)
	if err != nil {
		return err
	}
	defer tx.Rollback()

	queryUserCore := `
		UPDATE user_cores SET email_confirmed = $1 WHERE user_uid = $2
	`

	queryUserActive := `
		UPDATE user_actives SET online = true WHERE user_uid = $1
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	if _, err := tx.ExecContext(ctx, queryUserCore, confirmed, userUid); err != nil {
		return err
	}

	if _, err := tx.ExecContext(ctx, queryUserActive, userUid); err != nil {
		return err
	}

	if err := tx.Commit(); err != nil {
		return err
	}

	return nil
}

func (s *UserStore) Update_UserEmailByUid(ctx context.Context, userUid uint64, email string) error {
	query := `
		UPDATE user_cores SET email = $1 WHERE user_uid = $2
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	_, err := s.db.ExecContext(ctx, query, email, userUid)
	if err != nil {
		return err
	}

	return nil
}

func (s *UserStore) Update_UserCoreByUid(ctx context.Context, tx *sql.Tx, user *models.UserProfileUpd) error {
	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	fields := []string{}
	values := []interface{}{}
	i := 1

	if user.Email != nil {
		fields = append(fields, fmt.Sprintf("email=$%d", i))
		values = append(values, *user.Email)
		i++
	}

	if user.Password != nil {
		fields = append(fields, fmt.Sprintf("password=$%d", i))
		values = append(values, *user.Password)
		i++
	}

	if len(fields) == 0 {
		return nil
	}

	query := fmt.Sprintf(`
		UPDATE user_cores SET %s WHERE user_uid = $%d
	`, strings.Join(fields, ", "), i)

	values = append(values, user.UserUid)

	_, err := tx.ExecContext(ctx, query, values...)
	if err != nil {
		return err
	}

	return nil
}

func (s *UserStore) Update_UserProfileByUid(ctx context.Context, tx *sql.Tx, user *models.UserProfileUpd) error {
	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	fields := []string{}
	values := []interface{}{}
	i := 1

	if user.Username != nil {
		fields = append(fields, fmt.Sprintf("username=$%d", i))
		values = append(values, *user.Username)
		i++
	}

	if user.Name != nil {
		fields = append(fields, fmt.Sprintf("name=$%d", i))
		values = append(values, *user.Name)
		i++
	}

	if len(fields) == 0 {
		return nil
	}

	query := fmt.Sprintf(`
		UPDATE user_profiles SET %s WHERE user_uid = $%d
	`, strings.Join(fields, ", "), i)

	values = append(values, user.UserUid)

	_, err := tx.ExecContext(ctx, query, values...)
	if err != nil {
		return err
	}

	return nil
}

func (s *UserStore) Update_UserAvatarByUid(ctx context.Context, userUid uint64, avatar string) error {
	query := `
		UPDATE user_profiles SET avatar = $1 WHERE user_uid = $2
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	_, err := s.db.ExecContext(ctx, query, avatar, userUid)
	if err != nil {
		return err
	}

	return nil
}

func (s *UserStore) Delete_UserByUid(ctx context.Context, userUid uint64) error {
	query := `
		DELETE FROM user_cores WHERE user_uid = $1
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	del, err := s.db.ExecContext(ctx, query, userUid)
	if err != nil {
		return err
	}

	delRows, _ := del.RowsAffected()
	if delRows == 0 {
		return httperr.Err_NotDeleted
	}

	return nil
}
