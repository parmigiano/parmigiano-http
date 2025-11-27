package chats

import (
	"net/http"
	"parmigiano/http/pkg/httpx"
	"parmigiano/http/pkg/httpx/httperr"
	"parmigiano/http/types"
	"strconv"

	"github.com/gorilla/mux"
)

func (h *Handler) ChatsGetGroupHistoryHandler(w http.ResponseWriter, r *http.Request) error {
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

	chatIdParam := mux.Vars(r)["chatId"]
	chatId, err := strconv.ParseInt(chatIdParam, 10, 64)
	if err != nil {
		return httperr.BadRequest("неверный chatId")
	}

	chat, err := h.Store.Chats.Get_ChatGroupOrChannel(ctx, uint64(chatId))
	if err != nil {
		return httperr.Db(ctx, err)
	}

	if chat == nil {
		return httperr.NotFound("чат не найден")
	}

	if chat.ChatType != "group" {
		return httperr.BadRequest("этот чат не является группой")
	}

	ok, err := h.Store.Chats.Get_IsUserChatMember(ctx, uint64(chatId), authToken.User.UserUid)
	if err != nil {
		return httperr.Db(ctx, err)
	}

	if !ok {
		return httperr.Forbidden("вы не состоите в этом чате")
	}

	messages, err := h.Store.Messages.Get_MessagesHistoryByChatId(ctx, uint64(chatId), authToken.User.UserUid, limit, offset)
	if err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	if messages != nil {
		reverseMessages(messages)
	}

	httpx.HttpResponseWithETag(w, r, http.StatusOK, messages)
	return nil
}
