package messages

import (
	"net/http"
	"parmigiano/http/pkg/httpx"
	"parmigiano/http/pkg/httpx/httperr"
	"parmigiano/http/types"
	"strconv"

	"github.com/gorilla/mux"
)

func (h *Handler) MessagesGetHistoryHandler(w http.ResponseWriter, r *http.Request) error {
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

	messages, err := h.Store.Messages.Get_MessagesHistoryByReceiver(ctx, authToken.User.UserUid, uint64(senderUid))
	if err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	httpx.HttpResponseWithETag(w, r, http.StatusOK, messages)
	return nil
}
