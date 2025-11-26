package chats

import (
	"net/http"
	"parmigiano/http/handler/wsocket"
	"parmigiano/http/infra/constants"
	"parmigiano/http/pkg/httpx"
	"parmigiano/http/pkg/httpx/httperr"
	"parmigiano/http/types"

	"github.com/go-playground/validator"
)

func (h *Handler) ChatsUpdateBlockedHandler(w http.ResponseWriter, r *http.Request) error {
	ctx := r.Context()
	authToken := ctx.Value("identity").(*types.AuthToken)

	var payload *ChatUpdateBlockedPayload

	if err := httpx.HttpParse(r, &payload); err != nil {
		h.Logger.Error("%v", err)
		return httperr.BadRequest(err.Error())
	}

	if err := httpx.Validate.Struct(payload); err != nil {
		h.Logger.Error("%v", err)
		if _, ok := err.(validator.ValidationErrors); ok {
			return httperr.BadRequest(httpx.ValidateMsg(err))
		}

		return httperr.BadRequest("не все поля заполнены")
	}

	ok, err := h.Store.Chats.Get_IsUserChatMember(ctx, payload.ChatId, authToken.User.UserUid)
	if err != nil {
		return httperr.Db(ctx, err)
	}

	if !ok {
		return httperr.Forbidden("вы не состоите в этом чате")
	}

	if err := h.Store.Chats.Update_ChatSettingsBlocked(ctx, payload.Blocked, payload.ChatId); err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	// send event 'chat_blocked' for all users
	go func(chatIdP uint64, blockedp bool) {
		hub := wsocket.GetHub()
		hub.Broadcast(map[string]any{
			"event": constants.EVENT_CHAT_BLOCKED,
			"data": map[string]any{
				"chat_id": chatIdP,
				"blocked": blockedp,
			},
		})
	}(payload.ChatId, payload.Blocked)

	httpx.HttpResponse(w, r, http.StatusNoContent, nil)
	return nil
}
