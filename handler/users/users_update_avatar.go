package users

import (
	"fmt"
	"io"
	"net/http"
	"os"
	"parmigiano/http/handler/wsocket"
	"parmigiano/http/infra/constants"
	"parmigiano/http/pkg/httpx"
	"parmigiano/http/pkg/httpx/httperr"
	"parmigiano/http/pkg/s3"
	"parmigiano/http/types"
	"path/filepath"
)

func (h *Handler) UserUpdateAvatarHandler(w http.ResponseWriter, r *http.Request) error {
	ctx := r.Context()
	authToken := ctx.Value("identity").(*types.AuthToken)

	file, handler, err := r.FormFile("avatar")
	if err != nil {
		return httperr.BadRequest("файл не найден")
	}
	defer file.Close()

	// check prepare file
	switch handler.Header.Get("Content-Type") {
	case "image/png", "image/jpeg", "image/gif":
		// continue
	default:
		return httperr.BadRequest("только PNG, JPG или GIF разрешены")
	}

	tempPath := filepath.Join(os.TempDir(), handler.Filename)

	tempFile, err := os.Create(tempPath)
	if err != nil {
		return httperr.InternalServerError("ошибка создания временного файла")
	}

	defer func() {
		tempFile.Close()
		os.Remove(tempPath)
	}()

	_, err = io.Copy(tempFile, file)
	if err != nil {
		return httperr.InternalServerError("ошибка записи файла")
	}

	url, err := s3.UploadImageFile(fmt.Sprintf("avatar_user_uid_%d", authToken.User.UserUid), tempPath, handler.Header.Get("Content-Type"))
	if err != nil {
		h.Logger.Error("%v", err)
		return httperr.InternalServerError("ошибка загрузки файла")
	}

	if err := h.Store.Users.Update_UserAvatarByUid(ctx, authToken.User.UserUid, url); err != nil {
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

	// send event 'user_avatar_updated' for all users
	go func(userUid uint64, avatarUrl string) {
		hub := wsocket.GetHub()
		hub.Broadcast(map[string]any{
			"event": constants.EVENT_USER_AVATAR_UPDATED,
			"data": map[string]any{
				"user_uid": userUid,
				"url":      avatarUrl,
			},
		})
	}(authToken.User.UserUid, url)

	httpx.HttpResponse(w, r, http.StatusOK, url)
	return nil
}
