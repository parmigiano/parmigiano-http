package chats

type ChatUpdateBlockedPayload struct {
	ChatId  uint64 `json:"chat_id" validate:"required"`
	Blocked bool   `json:"blocked"`
}
