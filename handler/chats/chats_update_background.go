package chats

import (
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
	"strconv"

	"github.com/gorilla/mux"
)

func (h *Handler) ChatsUpdateCustomBackgroundHandler(w http.ResponseWriter, r *http.Request) error {
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

	file, handler, err := r.FormFile("background")
	if err != nil {
		return httperr.BadRequest("файл не был найден")
	}
	defer file.Close()

	tempPath := filepath.Join(os.TempDir(), handler.Filename)
	tempFile, err := os.Create(tempPath)
	if err != nil {
		return httperr.InternalServerError("ошибка создания временного файла")
	}
	defer func() {
		tempFile.Close()
		os.Remove(tempPath)
	}()

	if _, err := io.Copy(tempFile, file); err != nil {
		return httperr.InternalServerError("ошибка записи файла")
	}

	url, err := s3.UploadImageFile(uint64(chatId), tempPath, handler.Header.Get("Content-Type"))
	if err != nil {
		h.Logger.Error("%v", err)
		return httperr.InternalServerError("ошибка загрузки файла")
	}

	chatSetting, err := h.Store.Chats.Get_ChatSettingByChatId(ctx, uint64(chatId))
	if err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	if err := h.Store.Chats.Update_ChatSettingCustomBackground(ctx, url, uint64(chatId)); err != nil {
		h.Logger.Error("%v", err)
		return httperr.Db(ctx, err)
	}

	go func(old *string) {
		if old != nil {
			if err := s3.DeleteFile(*old); err != nil {
				h.Logger.Warning("Failed to delete chat background: %v", err)
			}
		}
	}(chatSetting.CustomBackground)

	go func(chatIdP uint64, urlp string) {
		wsocket.GetHub().Broadcast(map[string]any{
			"event": constants.EVENT_CHAT_BACKGROUND_UPDATED,
			"data": map[string]any{
				"chat_uid": chatIdP,
				"url":      urlp,
			},
		})
	}(uint64(chatId), url)

	httpx.HttpResponse(w, r, http.StatusOK, url)
	return nil
}
