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

func (s *MessageStore) Create_Message(tx *sql.Tx, ctx context.Context, message *models.Message) (uint64, error) {
	var messageId uint64

	query := `
		INSERT INTO messages (chat_id, sender_uid, content, content_type) VALUES ($1, $2, $3, $4) RETURNING id
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	// message
	if err := tx.QueryRowContext(ctx, query, message.ChatID, message.SenderUid, message.Content, message.ContentType).Scan(&messageId); err != nil {
		return 0, err
	}

	return messageId, nil
}

func (s *MessageStore) Create_MessageStatus(tx *sql.Tx, ctx context.Context, messageId, receiverUid uint64) (*time.Time, error) {
	var deliveredAt time.Time

	query := `
		INSERT INTO message_statuses (message_id, receiver_uid) VALUES ($1, $2) RETURNING delivered_at
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	// message status
	if err := tx.QueryRowContext(ctx, query, messageId, receiverUid).Scan(&deliveredAt); err != nil {
		return nil, err
	}

	return &deliveredAt, nil
}

func (s *MessageStore) Get_MessagesHistoryByChatId(ctx context.Context, chatId, myUserUid uint64) (*[]models.OnesMessage, error) {
	messages := []models.OnesMessage{}

	query := `
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
		LEFT JOIN message_statuses
			ON message_statuses.message_id = messages.id
			AND message_statuses.receiver_uid = $2
		LEFT JOIN message_edits
			ON message_edits.message_id = messages.id
		WHERE messages.chat_id = $1
		ORDER BY messages.created_at ASC;
	`

	ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
	defer cancel()

	rows, err := s.db.QueryContext(ctx, query, chatId, myUserUid)
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
