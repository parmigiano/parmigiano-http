package users

import (
	"net/http"
	"parmigiano/http/pkg/httpx"
	"parmigiano/http/pkg/httpx/httperr"
	"parmigiano/http/types"

	"github.com/gorilla/mux"
)

func (h *Handler) UsersFindByUsernameHandler(w http.ResponseWriter, r *http.Request) error {
	ctx := r.Context()
	authToken := ctx.Value("identity").(*types.AuthToken)

	username := mux.Vars(r)["username"]
	if username == "" {
		return httperr.NotFound("пользователь не был найден")
	}

	users, err := h.Store.Chats.Get_ChatsBySearchUsername(ctx, authToken.User.UserUid, username)
	if err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	httpx.HttpResponseWithETag(w, r, http.StatusOK, users)
	return nil
}
