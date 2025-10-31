package users

import (
	"net/http"
	"parmigiano/http/pkg/httpx"
	"parmigiano/http/types"
)

func (h *Handler) GetUserMeHandler(w http.ResponseWriter, r *http.Request) error {
	ctx := r.Context()
	authToken := ctx.Value("identity").(*types.AuthToken)

	httpx.HttpResponseWithETag(w, r, http.StatusOK, authToken.User)
	return nil
}
