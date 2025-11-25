package chats

import (
	"net/http"
	"parmigiano/http/pkg/httpx"
	"parmigiano/http/pkg/httpx/httperr"

	"github.com/go-playground/validator"
)

func (h *Handler) ChatsUpdateBlockedHandler(w http.ResponseWriter, r *http.Request) error {
	ctx := r.Context()

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

	if err := h.Store.Chats.Update_ChatSettingsBlocked(ctx, payload.Blocked, payload.ChatId); err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	httpx.HttpResponse(w, r, http.StatusOK, "Задний фон обновлен!")
	return nil
}
