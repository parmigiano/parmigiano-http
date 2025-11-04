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

func (s *MessageStore) Get_MessagesHistoryByReceiver(ctx context.Context, receiverUid, senderUid uint64) (*[]models.OnesMessage, error) {
	messages := []models.OnesMessage{}

	query := `
		SELECT
			messages.id,
			messages.sender_uid,
			messages.receiver_uid,
			messages.content,
			messages.content_type,
			messages.is_edited,
			messages.is_pinned,
			COALESCE(message_statuses.delivered_at, messages.created_at) AS delivered_at,
			message_statuses.read_at,
			message_edits.new_content AS edit_content
		FROM messages
		LEFT JOIN message_statuses
			ON message_statuses.message_id = messages.id
			AND message_statuses.receiver_uid = $1
		LEFT JOIN message_edits
			ON message_edits.message_id = messages.id
		WHERE
			(messages.sender_uid = $1 AND messages.receiver_uid = $2)
			OR (messages.sender_uid = $2 AND messages.receiver_uid = $1)
		ORDER BY messages.created_at ASC
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	rows, err := s.db.QueryContext(ctx, query, receiverUid, senderUid)
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
			&message.SenderUid,
			&message.ReceiverUid,
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
