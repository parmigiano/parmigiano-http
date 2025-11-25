package wsocket

import (
	"context"
	"parmigiano/http/infra/constants"
	"parmigiano/http/infra/store/postgres/models"
	"time"
)

func (h *WSHandler) handleIncomingRequest(c *Client, msg map[string]any) {
	event := msg["event"].(string)
	data := msg["data"].(map[string]any)

	switch event {
	case constants.EVENT_MESSAGE_SEND:
		h.handleMessageSend(c, data)
	}
}

func (h *WSHandler) handleMessageSend(client *Client, data map[string]any) {
	ctx := context.Background()

	chatId := uint64(data["chat_id"].(float64))
	content := data["content"].(string)
	contentType := data["content_type"].(string)

	tx, err := h.Db.BeginTx(ctx, nil)
	if err != nil {
		h.Logger.Error("%v", err)
		return
	}
	defer tx.Rollback()

	Message := models.Message{
		ChatID:      chatId,
		SenderUid:   client.UserUid,
		Content:     content,
		ContentType: contentType,
	}

	msgId, err := h.Store.Messages.Create_Message(tx, ctx, &Message)
	if err != nil {
		h.Logger.Error("%v", err)
		return
	}

	members, err := h.Store.Chats.Get_ChatMembers(ctx, chatId, client.UserUid)
	if err != nil {
		h.Logger.Error("%v", err)
		return
	}

	if members == nil {
		if err := tx.Commit(); err != nil {
			h.Logger.Error("%v", err)
		}

		return
	}

	var deliveredAtG time.Time

	for _, m := range *members {
		deliveredAt, err := h.Store.Messages.Create_MessageStatus(tx, ctx, msgId, m)
		if err != nil || deliveredAt == nil {
			h.Logger.Error("%v", err)
			continue
		}

		if deliveredAt != nil {
			deliveredAtG = *deliveredAt
		}
	}

	if err := tx.Commit(); err != nil {
		h.Logger.Error("%v", err)
		return
	}

	payload := map[string]any{
		"event": constants.EVENT_NEW_MESSAGE,
		"data": map[string]any{
			"chat_id":      chatId,
			"message_id":   msgId,
			"sender_uid":   client.UserUid,
			"content":      content,
			"content_type": contentType,
			"delivered_at": deliveredAtG,
		},
	}

	hub := GetHub()
	for _, member := range *members {
		hub.SendToUser(member, payload)
	}
}
