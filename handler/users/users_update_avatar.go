package users

import (
	"io"
	"net/http"
	"os"
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

	tempPath := filepath.Join(os.TempDir(), handler.Filename)

	tempFile, err := os.Create(tempPath)
	if err != nil {
		return httperr.InternalServerError("ошибка создания временного файла")
	}
	defer tempFile.Close()
	defer os.Remove(tempPath)

	_, err = io.Copy(tempFile, file)
	if err != nil {
		return httperr.InternalServerError("ошибка записи файла")
	}

	url, err := s3.UploadImageFile(authToken.User.UserUid, tempPath, handler.Header.Get("Content-Type"))
	if err != nil {
		h.Logger.Error("%v", err)
		return httperr.InternalServerError("ошибка загрузки файла")
	}

	if err := h.Store.Users.Update_UserAvatarByUid(ctx, authToken.User.UserUid, url); err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	if authToken.User.Avatar != nil || *authToken.User.Avatar != "" {
		if err := s3.DeleteFile(*authToken.User.Avatar); err != nil {
			h.Logger.Warning("Failed to delete avatar: %v", err)
		}
	}

	httpx.HttpResponse(w, r, http.StatusOK, url)
	return nil
}
