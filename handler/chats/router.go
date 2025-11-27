package chats

import (
	"net/http"
	"parmigiano/http/middleware"
	"parmigiano/http/pkg/httpx"

	"github.com/gorilla/mux"
)

func (h *Handler) RegisterRoutes(router *mux.Router) {
	chatRouter := router.PathPrefix("/chats").Subrouter()
	chatRouter.Use(middleware.IsAuthenticatedMiddleware(h.BaseHandler))

	// access: все
	// получение всех чатов пользователя
	// OR ?username={username}
	chatRouter.Handle("", httpx.ErrorHandler(h.GetChatsHandler)).Methods(http.MethodGet)

	// access: все
	// история для private (1:1)
	// ?offset={}
	// {companionUid} - второй пользователь
	chatRouter.Handle("/private/{companionUid:[0-9]+}/history", middleware.RequireEmailConfirmed(
		httpx.ErrorHandler(h.ChatsGetPrivateHistoryHandler),
	)).Methods(http.MethodGet)

	// access: все
	// история для group (1:many)
	// ?offset={}
	// {chatId} - ID чата
	chatRouter.Handle("/group/{chatId:[0-9]+}/history", middleware.RequireEmailConfirmed(
		httpx.ErrorHandler(h.ChatsGetGroupHistoryHandler),
	)).Methods(http.MethodGet)

	// access: все
	// история для channel (1:many)
	// ?offset={}
	// {chatId} - ID чата
	chatRouter.Handle("/channel/{chatId:[0-9]+}/history", middleware.RequireEmailConfirmed(
		httpx.ErrorHandler(h.ChatsGetChannelHistoryHandler),
	)).Methods(http.MethodGet)

	// access: все
	// получить настройки чата
	chatRouter.Handle("/{chatId:[0-9]+}/s", httpx.ErrorHandler(h.GetChatSettingsHandler)).Methods(http.MethodGet)

	// access: все
	// заблокировать чат
	chatRouter.Handle("/{chatId:[0-9]+}/s/blocked", middleware.RequireEmailConfirmed(
		httpx.ErrorHandler(h.ChatsUpdateBlockedHandler),
	)).Methods(http.MethodPatch)

	// access: все
	// обновить background чата
	chatRouter.Handle("/{chatId:[0-9]+}/s/cbackground", middleware.RequireEmailConfirmed(
		httpx.ErrorHandler(h.ChatsUpdateCustomBackgroundHandler),
	)).Methods(http.MethodPost)
}
