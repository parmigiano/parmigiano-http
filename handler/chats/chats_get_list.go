package chats

import (
	"net/http"
	"parmigiano/http/infra/store/postgres/models"
	"parmigiano/http/pkg/httpx"
	"parmigiano/http/pkg/httpx/httperr"
	"parmigiano/http/types"
)

func (h *Handler) GetChatsHandler(w http.ResponseWriter, r *http.Request) error {
	ctx := r.Context()
	authToken := ctx.Value("identity").(*types.AuthToken)

	searchParams := r.URL.Query()
	username := searchParams.Get("username")

	var (
		chats *[]models.ChatMinimalWithLMessage
		err   error
	)

	// fond by username
	if username != "" {
		chats, err = h.Store.Chats.Get_ChatsBySearchUsername(ctx, authToken.User.UserUid, username)
		if err != nil {
			h.Logger.Error("%v", err)
			return httperr.Db(ctx, err)
		}
	} else { // get all chats
		chats, err = h.Store.Chats.Get_ChatsMyHistory(ctx, authToken.User.UserUid)
		if err != nil {
			h.Logger.Error("%v", err)
			return httperr.Db(ctx, err)
		}
	}

	httpx.HttpResponseWithETag(w, r, http.StatusOK, chats)
	return nil
}
