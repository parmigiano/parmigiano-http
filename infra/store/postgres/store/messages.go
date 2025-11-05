package store

import (
	"context"
	"database/sql"
	"errors"
	"parmigiano/http/infra/logger"
	"parmigiano/http/infra/store/postgres/models"
	"time"
)

type MessageStore struct {
	db     *sql.DB
	logger *logger.Logger
}

func (s *MessageStore) Get_MessagesHistoryByReceiver(ctx context.Context, myUserUid, otherUserUid uint64) (*[]models.OnesMessage, error) {
	messages := []models.OnesMessage{}

	query := `
		WITH chat AS (
			SELECT chats.id as chat_id
			FROM chats
			JOIN chat_members AS cm1 ON cm1.chat_id = chats.id AND cm1.user_uid = $1
			JOIN chat_members AS cm2 ON cm2.chat_id = chats.id AND cm2.user_uid = $2
			WHERE chats.chat_type = 'private'
			LIMIT 1
		)
		SELECT
			messages.id,
			messages.chat_id,
			messages.sender_uid,
			messages.content,
			messages.content_type,
			messages.is_edited,
			messages.is_pinned,
			COALESCE(message_statuses.delivered_at, messages.created_at) AS delivered_at,
			message_statuses.read_at,
			message_edits.new_content AS edit_content
		FROM messages
		JOIN chat ON messages.chat_id = chat.chat_id
		LEFT JOIN message_statuses
			ON message_statuses.message_id = messages.id
			AND message_statuses.receiver_uid = $1
		LEFT JOIN message_edits
			ON message_edits.message_id = messages.id
		ORDER BY messages.created_at ASC;
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	rows, err := s.db.QueryContext(ctx, query, myUserUid, otherUserUid)
	if err != nil {
		if errors.Is(sql.ErrNoRows, err) {
			return nil, nil
		}

		return nil, err
	}
	defer rows.Close()

	for rows.Next() {
		var message models.OnesMessage

		err := rows.Scan(
			&message.ID,
			&message.ChatID,
			&message.SenderUid,
			&message.Content,
			&message.ContentType,
			&message.IsEdited,
			&message.IsPinned,
			&message.DeliveredAt,
			&message.ReadAt,
			&message.EditContent,
		)

		if err != nil {
			return nil, err
		}

		messages = append(messages, message)
	}

	if err := rows.Err(); err != nil {
		return nil, err
	}

	return &messages, nil
}
