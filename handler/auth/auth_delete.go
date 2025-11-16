package auth

import (
	"net/http"
	"parmigiano/http/handler/wsocket"
	"parmigiano/http/infra/constants"
	"parmigiano/http/pkg/httpx"
	"parmigiano/http/pkg/httpx/httperr"
	"parmigiano/http/pkg/s3"
	"parmigiano/http/types"
)

func (h *Handler) AuthDeleteHandler(w http.ResponseWriter, r *http.Request) error {
	ctx := r.Context()
	authToken := ctx.Value("identity").(*types.AuthToken)

	if err := h.Store.Users.Delete_UserByUid(ctx, authToken.User.UserUid); err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	go func(avatar *string) {
		if avatar != nil {
			if err := s3.DeleteFile(*avatar); err != nil {
				h.Logger.Warning("Failed to delete avatar: %v", err)
			}
		}
	}(authToken.User.Avatar)

	// send event 'user_deleted' for all users
	go func(userUid uint64) {
		hub := wsocket.GetHub()
		hub.Broadcast(map[string]any{
			"event": constants.EVENT_USER_DELETED,
			"data": map[string]any{
				"user_uid": userUid,
			},
		})
	}(authToken.User.UserUid)

	httpx.HttpResponse(w, r, http.StatusOK, "Account has been deleted")
	return nil
}
