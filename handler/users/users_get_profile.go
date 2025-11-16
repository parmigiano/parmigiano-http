package users

import (
	"fmt"
	"net/http"
	"parmigiano/http/pkg/httpx"
	"parmigiano/http/pkg/httpx/httperr"
	"strconv"

	"github.com/gorilla/mux"
)

func (h *Handler) GetUserProfileHandler(w http.ResponseWriter, r *http.Request) error {
	ctx := r.Context()

	uidParam := mux.Vars(r)["uid"]
	if uidParam == "" {
		return httperr.NotFound("пользователь не был найден")
	}

	uid, err := strconv.Atoi(uidParam)
	if err != nil || uid < 1 {
		return httperr.BadRequest(fmt.Sprintf("пользователь с %s не был найден", uidParam))
	}

	user, err := h.Store.Users.Get_UserInfoByUserUid(ctx, uint64(uid))
	if err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	httpx.HttpResponseWithETag(w, r, http.StatusOK, user)
	return nil
}
