package store

import (
	"context"
	"database/sql"
	"errors"
	"parmigiano/http/infra/logger"
	"parmigiano/http/infra/store/postgres/models"
	"time"
)

type ChatStore struct {
	db     *sql.DB
	logger *logger.Logger
}

func (s *ChatStore) Create_Chat(ctx context.Context, chat *models.Chat) (uint64, error) {
	var chatId uint64

	query := `
		INSERT INTO chats (chat_type, title) VALUES ($1, $2) RETURNING id
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	err := s.db.QueryRowContext(ctx, query, chat.ChatType, chat.Title).Scan(&chatId)
	if err != nil {
		return 0, err
	}

	return chatId, nil
}

func (s *ChatStore) Create_ChatMember(ctx context.Context, member *models.ChatMember) error {
	query := `
		INSERT INTO chat_members (chat_id, user_uid, role) VALUES ($1, $2, $3)
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	_, err := s.db.ExecContext(ctx, query, member.ChatID, member.UserUid, member.Role)
	if err != nil {
		return err
	}

	return nil
}

func (s *ChatStore) Get_ChatPrivateByUser(ctx context.Context, myUserUid, otherUserUid uint64) (*models.Chat, error) {
	chat := models.Chat{}

	query := `
		SELECT
			chats.*
		FROM chats
		JOIN chat_members AS cm1 ON cm1.chat_id = chats.id AND cm1.user_uid = $1
		JOIN chat_members AS cm2 ON cm2.chat_id = chats.id AND cm2.user_uid = $2
		WHERE chats.chat_type = 'private'
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	row := s.db.QueryRowContext(ctx, query, myUserUid, otherUserUid)

	err := row.Scan(
		&chat.ID,
		&chat.CreatedAt,
		&chat.UpdatedAt,
		&chat.ChatType,
		&chat.Title,
	)

	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return nil, nil
		}

		return nil, err
	}

	return &chat, nil
}

func (s *ChatStore) Get_ChatsMyHistory(ctx context.Context, userUid uint64) (*[]models.ChatMinimalWithLMessage, error) {
	chats := []models.ChatMinimalWithLMessage{}

	query := `
		SELECT DISTINCT
			chats.id,
			user_profiles.name,
			user_profiles.username,
			user_profiles.avatar,
			user_cores.user_uid,
			user_cores.email,
			user_actives.online,
			user_actives.updated_at as last_online_date,
			last_message.content AS last_message,
			last_message.created_at AS last_message_date,
			COALESCE(unread_count.count, 0) AS unread_message_count
		FROM messages
		JOIN chats ON messages.chat_id = chats.id
		JOIN chat_members cm1 ON cm1.chat_id = chats.id
		JOIN chat_members cm2 ON cm2.chat_id = chats.id
		JOIN user_cores ON (user_cores.user_uid = cm1.user_uid OR user_cores.user_uid = cm2.user_uid)
		LEFT JOIN user_profiles ON user_cores.user_uid = user_profiles.user_uid
		LEFT JOIN user_actives ON user_cores.user_uid = user_actives.user_uid
		LEFT JOIN LATERAL (
			SELECT messages.content, messages.created_at
			FROM messages AS m2
			WHERE m2.chat_id = chats.id
			ORDER BY m2.created_at DESC
			LIMIT 1
		) AS last_message ON TRUE
		LEFT JOIN LATERAL (
			SELECT COUNT(*) AS count
			FROM messages AS m3
			LEFT JOIN message_statuses ms ON ms.message_id = m3.id AND ms.receiver_uid = $1
			WHERE m3.chat_id = chats.id AND m3.sender_uid != $1 AND (ms.read_at IS NULL)
		) AS unread_count ON TRUE
		WHERE $1 IN (cm1.user_uid, cm2.user_uid) AND user_cores.user_uid != $1
		ORDER BY last_message.created_at DESC
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
		var chat models.ChatMinimalWithLMessage

		err := rows.Scan(
			&chat.ID,
			&chat.Name,
			&chat.Username,
			&chat.Avatar,
			&chat.UserUid,
			&chat.Email,
			&chat.Online,
			&chat.LastOnlineDate,
			&chat.LastMessage,
			&chat.LastMessageDate,
			&chat.UnreadMessageCount,
		)

		if err != nil {
			return nil, err
		}

		chats = append(chats, chat)
	}

	if err := rows.Err(); err != nil {
		return nil, err
	}

	return &chats, nil
}

func (s *ChatStore) Get_ChatsBySearchUsername(ctx context.Context, myUserUid uint64, username string) (*[]models.ChatMinimalWithLMessage, error) {
	chats := []models.ChatMinimalWithLMessage{}

	query := `
		SELECT
			user_cores.id,
			user_profiles.name,
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
			FROM chats
			JOIN chat_members cm1 ON cm1.chat_id = chats.id AND cm1.user_uid = user_cores.user_uid
			JOIN chat_members cm2 ON cm2.chat_id = chats.id AND cm2.user_uid = $1
			JOIN messages ON messages.chat_id = chats.id
			WHERE chats.chat_type = 'private'
			ORDER BY messages.created_at DESC
    		LIMIT 1
		) AS last_message ON TRUE
		LEFT JOIN LATERAL (
			SELECT COUNT(*) AS count
			FROM chats
			JOIN chat_members cm1 ON cm1.chat_id = chats.id AND cm1.user_uid = user_cores.user_uid
			JOIN chat_members cm2 ON cm2.chat_id = chats.id AND cm2.user_uid = $1
			JOIN messages ON messages.chat_id = chats.id
			LEFT JOIN message_statuses
				ON message_statuses.message_id = messages.id AND message_statuses.receiver_uid = $1
			WHERE chats.chat_type = 'private'
				AND messages.sender_uid = user_cores.user_uid
      			AND (message_statuses.read_at IS NULL)
		) AS unread_count ON TRUE
		WHERE user_cores.user_uid != $1 AND (
			similarity(user_profiles.username, $2) > 0.6
		)
		ORDER BY similarity(user_profiles.username, $2) DESC, user_profiles.username ASC
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	rows, err := s.db.QueryContext(ctx, query, myUserUid, username)
	if err != nil {
		if errors.Is(sql.ErrNoRows, err) {
			return nil, nil
		}

		return nil, err
	}
	defer rows.Close()

	for rows.Next() {
		var chat models.ChatMinimalWithLMessage

		err := rows.Scan(
			&chat.ID,
			&chat.Name,
			&chat.Username,
			&chat.Avatar,
			&chat.UserUid,
			&chat.Email,
			&chat.Online,
			&chat.LastOnlineDate,
			&chat.LastMessage,
			&chat.LastMessageDate,
			&chat.UnreadMessageCount,
		)

		if err != nil {
			return nil, err
		}

		chats = append(chats, chat)
	}

	if err := rows.Err(); err != nil {
		return nil, err
	}

	return &chats, nil
}
