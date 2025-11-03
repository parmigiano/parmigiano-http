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
		INSERT INTO user_profiles (user_uid, avatar, username) VALUES ($1, $2, $3)
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	_, err := tx.ExecContext(ctx, query, user.UserUid, user.Avatar, user.Username)
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

func (s *UserStore) Get_UsersWithLMessage(ctx context.Context, userUid uint64) (*[]models.UserMinimalWithLMessage, error) {
	users := []models.UserMinimalWithLMessage{}

	query := `
		SELECT
			user_cores.id,
			user_profiles.username,
			user_profiles.avatar,
			user_cores.user_uid,
			user_cores.email,
			user_actives.online,
			user_actives.updated_at as last_online_date,
			last_message.content AS last_message,
			last_message.created_at AS last_message_date,
			COALESCE(unread_count.count, 0) AS unread_message_count
		FROM user_cores
		LEFT JOIN user_profiles ON user_cores.user_uid = user_profiles.user_uid
		LEFT JOIN user_actives ON user_cores.user_uid = user_actives.user_uid
		LEFT JOIN LATERAL (
			SELECT messages.content, messages.created_at
			FROM messages
			WHERE
				(messages.sender_uid = user_cores.user_uid AND messages.receiver_uid = $1)
				OR (messages.sender_uid = $1 AND messages.receiver_uid = user_cores.user_uid)
				ORDER BY messages.created_at DESC
				LIMIT 1
		) AS last_message ON TRUE
		LEFT JOIN LATERAL (
			SELECT COUNT(*) AS count
			FROM messages
			INNER JOIN message_statuses ON message_statuses.message_id = messages.id
			WHERE
				messages.sender_uid = user_cores.user_uid
				AND messages.receiver_uid = $1
				AND message_statuses.receiver_uid = $1
				AND message_statuses.read_at IS NULL
		) AS unread_count ON TRUE
		WHERE user_cores.user_uid != $1
		ORDER BY COALESCE(last_message.created_at, user_cores.created_at) DESC
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	rows, err := s.db.QueryContext(ctx, query, userUid)
	if err != nil {
		if errors.Is(sql.ErrNoRows, err) {
			return nil, nil
		}

		return nil, err
	}
	defer rows.Close()

	for rows.Next() {
		var user models.UserMinimalWithLMessage

		err := rows.Scan(
			&user.ID,
			&user.Username,
			&user.Avatar,
			&user.UserUid,
			&user.Email,
			&user.Online,
			&user.LastOnlineDate,
			&user.LastMessage,
			&user.LastMessageDate,
			&user.UnreadMessageCount,
		)

		if err != nil {
			return nil, err
		}

		users = append(users, user)
	}

	if err := rows.Err(); err != nil {
		return nil, err
	}

	return &users, nil
}

func (s *UserStore) Get_UserWithLMessage(ctx context.Context, userUid uint64) (*models.UserMinimalWithLMessage, error) {
	user := models.UserMinimalWithLMessage{}

	query := `
		SELECT
			user_cores.id,
			user_profiles.username,
			user_profiles.avatar,
			user_cores.user_uid,
			user_cores.email,
			user_actives.online,
			user_actives.updated_at as last_online_date,
			last_message.content AS last_message,
			last_message.created_at AS last_message_date,
			0 AS unread_message_count
		FROM user_cores
		LEFT JOIN user_profiles ON user_cores.user_uid = user_profiles.user_uid
		LEFT JOIN user_actives ON user_cores.user_uid = user_actives.user_uid
		LEFT JOIN LATERAL (
			SELECT messages.content, messages.created_at
			FROM messages
			WHERE
				messages.sender_uid = user_cores.user_uid
                OR messages.receiver_uid = user_cores.user_uid
			ORDER BY messages.created_at DESC
            LIMIT 1
		) AS last_message ON TRUE
		WHERE user_cores.user_uid = $1
		LIMIT 1
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	row := s.db.QueryRowContext(ctx, query, userUid)

	err := row.Scan(
		&user.ID,
		&user.Username,
		&user.Avatar,
		&user.UserUid,
		&user.Email,
		&user.Online,
		&user.LastOnlineDate,
		&user.LastMessage,
		&user.LastMessageDate,
		&user.UnreadMessageCount,
	)

	if err != nil {
		return nil, err
	}

	return &user, nil
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
