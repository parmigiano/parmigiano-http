package chats

import (
	"net/http"
	"parmigiano/http/pkg/httpx"
	"parmigiano/http/pkg/httpx/httperr"
	"parmigiano/http/types"
	"strconv"

	"github.com/gorilla/mux"
)

func (h *Handler) GetChatSettingsHandler(w http.ResponseWriter, r *http.Request) error {
	ctx := r.Context()
	authToken := ctx.Value("identity").(*types.AuthToken)

	chatIdParam := mux.Vars(r)["chatId"]
	if chatIdParam == "" {
		return httperr.BadRequest("неверный chat_uid")
	}

	chatId, err := strconv.Atoi(chatIdParam)
	if err != nil {
		return httperr.BadRequest("неверный chat_uid")
	}

	ok, err := h.Store.Chats.Get_IsUserChatMember(ctx, uint64(chatId), authToken.User.UserUid)
	if err != nil {
		return httperr.Db(ctx, err)
	}

	if !ok {
		return httperr.Forbidden("вы не состоите в этом чате")
	}

	chatSetting, err := h.Store.Chats.Get_ChatSettingByChatId(ctx, uint64(chatId))
	if err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	httpx.HttpResponseWithETag(w, r, http.StatusOK, chatSetting)
	return nil
}
