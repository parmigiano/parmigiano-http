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

var limit int = 30

func (h *Handler) ChatsGetPrivateHistoryHandler(w http.ResponseWriter, r *http.Request) error {
	ctx := r.Context()
	authToken := ctx.Value("identity").(*types.AuthToken)

	searchParams := r.URL.Query()
	offsetParam := searchParams.Get("offset")

	if offsetParam == "" {
		offsetParam = "0"
	}

	offset, err := strconv.Atoi(offsetParam)
	if err != nil {
		return httperr.InternalServerError("invalid offset")
	}

	companionUidParam := mux.Vars(r)["companionUid"]
	if companionUidParam == "" {
		return httperr.BadRequest("отсутствует идентификатор отправителя")
	}

	companionUid, err := strconv.Atoi(companionUidParam)
	if err != nil {
		h.Logger.Error("%v", err)
		return httperr.InternalServerError("ошибка конфертации идентификатора отправителя")
	}

	chat, err := h.Store.Chats.Get_ChatPrivate(ctx, authToken.User.UserUid, uint64(companionUid))
	if err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	if chat != nil {
		messages, err := h.Store.Messages.Get_MessagesHistoryByChatId(ctx, chat.ID, authToken.User.UserUid, limit, offset)
		if err != nil {
			h.Logger.Error("%v", err)
			return httperr.Db(ctx, err)
		}

		httpx.HttpResponseWithETag(w, r, http.StatusOK, messages)
		return nil
	}

	tx, err := h.Db.BeginTx(ctx, nil)
	if err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}
	defer func() {
		_ = tx.Rollback()
	}()

	chatId, err := h.Store.Chats.Create_Chat(tx, ctx, &models.Chat{
		ChatType: "private",
		Title:    nil,
	})
	if err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	members := []models.ChatMember{
		{ChatID: chatId, UserUid: authToken.User.UserUid, Role: "member"},
		{ChatID: chatId, UserUid: uint64(companionUid), Role: "member"},
	}

	for _, member := range members {
		if err := h.Store.Chats.Create_ChatMember(tx, ctx, &member); err != nil {
			h.Logger.Error("%v", err)
			return httperr.Db(ctx, err)
		}
	}

	if err := h.Store.Chats.Create_ChatSetting(tx, ctx, &models.ChatSetting{ChatID: chatId}); err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	if err := tx.Commit(); err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	httpx.HttpResponse(w, r, http.StatusOK, []models.Message{})
	return nil
}
