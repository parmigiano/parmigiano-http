package chats

import (
	"net/http"
	"parmigiano/http/infra/store/postgres/models"
	"parmigiano/http/pkg/httpx"
	"parmigiano/http/pkg/httpx/httperr"
	"parmigiano/http/types"
	"strconv"

	"github.com/gorilla/mux"
)

func (h *Handler) ChatsGetHistoryHandler(w http.ResponseWriter, r *http.Request) error {
	ctx := r.Context()
	authToken := ctx.Value("identity").(*types.AuthToken)

	senderUidParam := mux.Vars(r)["senderUid"]
	if senderUidParam == "" {
		return httperr.BadRequest("отсутствует идентификатор отправителя")
	}

	senderUid, err := strconv.Atoi(senderUidParam)
	if err != nil {
		h.Logger.Error("%v", err)
		return httperr.InternalServerError("ошибка конфертации идентификатора отправителя")
	}

	chat, err := h.Store.Chats.Get_ChatPrivateByUser(ctx, authToken.User.UserUid, uint64(senderUid))
	if err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	if chat != nil {
		messages, err := h.Store.Messages.Get_MessagesHistoryByReceiver(ctx, authToken.User.UserUid, uint64(senderUid))
		if err != nil {
			h.Logger.Error("%v", err)
			return httperr.Db(ctx, err)
		}

		httpx.HttpResponseWithETag(w, r, http.StatusOK, messages)
		return nil
	}

	chatId, err := h.Store.Chats.Create_Chat(ctx, &models.Chat{
		ChatType: "private",
		Title:    nil,
	})
	if err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	members := []models.ChatMember{
		{ChatID: chatId, UserUid: authToken.User.UserUid, Role: "member"},
		{ChatID: chatId, UserUid: uint64(senderUid), Role: "member"},
	}

	for _, member := range members {
		if err := h.Store.Chats.Create_ChatMember(ctx, &member); err != nil {
			h.Logger.Error("%v", err)
			return httperr.Db(ctx, err)
		}
	}

	httpx.HttpResponse(w, r, http.StatusOK, []models.Message{})
	return nil
}
