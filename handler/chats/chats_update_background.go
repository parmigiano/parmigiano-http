package chats

import (
	"errors"
	"fmt"
	"io"
	"net/http"
	"os"
	"parmigiano/http/handler/wsocket"
	"parmigiano/http/infra/constants"
	"parmigiano/http/infra/store/redis"
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
		return httperr.BadRequest("неверный chat_id")
	}

	chatId, err := strconv.Atoi(chatIdParam)
	if err != nil {
		return httperr.BadRequest("неверный chat_id")
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

	file, handler, err := r.FormFile("background")

	// delete background
	if errors.Is(err, http.ErrMissingFile) {
		if chatSetting.CustomBackground != nil {
			if err := s3.DeleteFile(*chatSetting.CustomBackground); err != nil {
				h.Logger.Warning("Failed to delete chat background: %v", err)
			}
		}

		if err := h.Store.Chats.Update_ChatSettingCustomBackground(ctx, nil, uint64(chatId)); err != nil {
			h.Logger.Error("%v", err)
			return httperr.Db(ctx, err)
		}

		// delete cache
		go func(chatIdP uint64) {
			if err := redis.DeleteChatSettingCache(chatIdP); err != nil {
				h.Logger.Error("%v", err)
			}
		}(uint64(chatId))

		// send event 'chat_background_updated' for all users
		wsocket.GetHub().Broadcast(map[string]any{
			"event": constants.EVENT_CHAT_BACKGROUND_UPDATED,
			"data": map[string]any{
				"chat_id": uint64(chatId),
				"url":     nil,
			},
		})

		httpx.HttpResponse(w, r, http.StatusOK, map[string]any{"url": nil})
		return nil
	}

	if err != nil {
		return httperr.BadRequest("ошибка получения файла")
	}

	defer file.Close()

	// check prepare file
	switch handler.Header.Get("Content-Type") {
	case "image/png", "image/jpeg":
		// continue
	default:
		return httperr.BadRequest("только PNG или JPG разрешены")
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

	if _, err := io.Copy(tempFile, file); err != nil {
		return httperr.InternalServerError("ошибка записи файла")
	}

	url, err := s3.UploadImageFile(fmt.Sprintf("chat_id_%d", chatId), tempPath, handler.Header.Get("Content-Type"))
	if err != nil {
		h.Logger.Error("%v", err)
		return httperr.InternalServerError("ошибка загрузки файла")
	}

	if err := h.Store.Chats.Update_ChatSettingCustomBackground(ctx, &url, uint64(chatId)); err != nil {
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

	// delete cache
	go func(chatIdP uint64) {
		if err := redis.DeleteChatSettingCache(chatIdP); err != nil {
			h.Logger.Error("%v", err)
		}
	}(uint64(chatId))

	// send event 'chat_background_updated' for all users
	go func(chatIdP uint64, urlp string) {
		hub := wsocket.GetHub()
		hub.Broadcast(map[string]any{
			"event": constants.EVENT_CHAT_BACKGROUND_UPDATED,
			"data": map[string]any{
				"chat_id": chatIdP,
				"url":     urlp,
			},
		})
	}(uint64(chatId), url)

	httpx.HttpResponse(w, r, http.StatusOK, url)
	return nil
}
